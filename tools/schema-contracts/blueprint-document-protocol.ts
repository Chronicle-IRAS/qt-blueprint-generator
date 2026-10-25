import fs from "node:fs/promises";
import path from "node:path";
import { readBlueprintDocument, type BlueprintDocumentReadResult } from "./blueprint-document-reader.js";
import { blueprintEditTargets, prepareBlueprintEdits, saveEditedBlueprintDocument, type BlueprintEdit, type BlueprintEditTargets, type BlueprintEditPreparationResult, type BlueprintEditedDocumentSaveResult } from "./blueprint-document-editor.js";
import { loadBlueprintDocument, type BlueprintDocumentLoadResult } from "./blueprint-document-store.js";
import { PluginRegistry } from "./plugin-registry.js";
import { SOURCE_LIMIT } from "./blueprint-reader-protocol.js";
import type { Diagnostic } from "./semantic-validator.js";
export type BlueprintWireEdit = Exclude<BlueprintEdit, {kind:"node-property"}> | {kind:"node-property"; sourceIndex:number; property:string; valueLiteral:string};
export type BlueprintDocumentRequest = {protocolVersion:2; requestId:string; registry:{descriptors:unknown;trustedLocks:unknown}} & (
  {operation:"load";directory:string} | {operation:"preview";sourceText:string;edits:readonly BlueprintWireEdit[]} |
  {operation:"save";directory:string;sourceText:string;expectedSha256:string;edits:readonly BlueprintWireEdit[]});
type ProtocolFailure = {status:"failed";code:string;message:string;diagnostics:readonly Diagnostic[];writeState:"not-written";canGenerate:false};
export interface BlueprintDocumentReply {
  protocolVersion:2; requestId:string; operation:string; canGenerate:false;
  result: BlueprintDocumentLoadResult | BlueprintEditPreparationResult | BlueprintEditedDocumentSaveResult | ProtocolFailure;
  readResult?: BlueprintDocumentReadResult; editTargets?: BlueprintEditTargets; writable?:boolean; readOnlyReason?:string;
}
const object = (v:unknown):v is Record<string,unknown> => v !== null && typeof v === "object" && !Array.isArray(v);
export async function handleBlueprintDocumentRequest(input:unknown):Promise<BlueprintDocumentReply> {
  const requestId=object(input)&&typeof input.requestId==="string"?input.requestId.slice(0,256):"";
  const operation=object(input)&&typeof input.operation==="string"?input.operation:"";
  const envelope={protocolVersion:2 as const,requestId,operation,canGenerate:false as const};
  const error=(code:string,message:string,location="/request"):BlueprintDocumentReply=>({...envelope,result:{status:"failed",code,message,diagnostics:[{code,message,severity:"error",path:location}],writeState:"not-written",canGenerate:false}});
  if(!object(input)||input.protocolVersion!==2||!requestId||input.requestId!==requestId||!["load","preview","save"].includes(operation)||!object(input.registry)||!("descriptors" in input.registry)||!("trustedLocks" in input.registry)) return error("PROTOCOL_INVALID","Expected version 2, request ID, operation and independent registry declarations");
  const registry=new PluginRegistry();const registration=registry.register(input.registry.descriptors,input.registry.trustedLocks);
  if(registration.diagnostics.length) return {...envelope,result:{status:"failed",code:"REGISTRY_INVALID",message:"Invalid registry declarations",diagnostics:registration.diagnostics,writeState:"not-written",canGenerate:false}};
  const display=(result:BlueprintDocumentReply["result"],readResult?:BlueprintDocumentReadResult):BlueprintDocumentReply=>({...envelope,result,...(readResult?{readResult,...(readResult.status==="read"?{editTargets:blueprintEditTargets(readResult,registry)}:{})}:{})});
  if(operation==="load") {
    if(typeof input.directory!=="string"||!path.isAbsolute(input.directory)) return error("PROTOCOL_INVALID","An absolute directory is required");
    let sourceText:string;
    try {
      const handle=await fs.open(path.join(input.directory,"blueprint.json"),"r");
      try { const stat=await handle.stat();if(!stat.isFile())return error("IO_ERROR","Blueprint must be a regular file");if(stat.size>SOURCE_LIMIT)return error("SOURCE_TOO_LARGE","Blueprint source exceeds 8 MiB");
        const bytes=Buffer.alloc(SOURCE_LIMIT+1);let bytesRead=0;
        while(bytesRead<bytes.length){const chunk=await handle.read(bytes,bytesRead,bytes.length-bytesRead,bytesRead);if(!chunk.bytesRead)break;bytesRead+=chunk.bytesRead;}
        if(bytesRead>SOURCE_LIMIT)return error("SOURCE_TOO_LARGE","Blueprint source exceeds 8 MiB");
        sourceText=new TextDecoder("utf-8",{fatal:true,ignoreBOM:true}).decode(bytes.subarray(0,bytesRead));
        if(sourceText.startsWith("\ufeff"))return error("ENCODING_INVALID","UTF-8 without BOM required");
      } finally {await handle.close();}
    } catch {return error("IO_ERROR","Cannot read blueprint.json as lossless UTF-8");}
    const result=await loadBlueprintDocument(input.directory,registry);
    if(result.status==="loaded")return {...display(result,result.readResult),writable:true};
    if(result.code==="DIRECTORY_UNSUITABLE"||result.code==="PATH_UNSAFE")return {...display(result,readBlueprintDocument(sourceText,registry)),writable:false,readOnlyReason:result.code};
    return display(result,result.readResult);
  }
  if(typeof input.sourceText!=="string"||!Array.isArray(input.edits)||input.edits.length>256) return error("PROTOCOL_INVALID","Source and bounded edit batch required");
  if(Buffer.byteLength(input.sourceText,"utf8")>SOURCE_LIMIT)return error("SOURCE_TOO_LARGE","Blueprint source exceeds 8 MiB");
  const edits:BlueprintEdit[]=[];
  for(const [index,value] of input.edits.entries()) {
    if(!object(value))return error("INVALID_EDIT","Edit must be an object");
    if(value.kind==="node-property") {
      const location=`/edits/${index}/valueLiteral`, target=`Node ${String(value.sourceIndex)} property ${String(value.property)}: `;
      if(typeof value.valueLiteral!=="string"||value.valueLiteral.length>SOURCE_LIMIT)return error("INVALID_EDIT",target+"bounded JSON scalar literal required",location);
      let scalar:unknown;try{scalar=JSON.parse(value.valueLiteral);}catch{return error("INVALID_EDIT",target+"invalid JSON scalar literal",location);}
      if(!(scalar===null||typeof scalar==="string"||typeof scalar==="boolean"||(typeof scalar==="number"&&Number.isFinite(scalar))))return error("INVALID_EDIT",target+"finite JSON scalar required",location);
      edits.push({...value,value:scalar} as BlueprintEdit);
    }else edits.push({...value} as BlueprintEdit);
  }
  const prepared=prepareBlueprintEdits(input.sourceText,edits,registry);
  if(prepared.status==="failed")return display(operation==="save"?{...prepared,writeState:"not-written"}:prepared);
  if(Buffer.byteLength(prepared.sourceText,"utf8")>SOURCE_LIMIT)return error("SOURCE_TOO_LARGE","Edited Blueprint source exceeds 8 MiB");
  if(operation==="preview")return display(prepared,prepared.readResult);
  if(typeof input.directory!=="string"||typeof input.expectedSha256!=="string")return error("PROTOCOL_INVALID","Save directory and baseline SHA-256 required");
  const result=await saveEditedBlueprintDocument({directory:input.directory,sourceText:input.sourceText,expectedSha256:input.expectedSha256,edits},registry);
  return display(result,result.status==="saved"?result.readResult:undefined);
}
