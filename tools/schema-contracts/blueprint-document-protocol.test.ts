import { readFileSync, mkdtempSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, expect, it } from "vitest";
import { handleBlueprintDocumentRequest } from "./blueprint-document-protocol.js";
const load = (name: string) => JSON.parse(readFileSync(new URL(`../../tests/fixtures/plugin-descriptors/${name}.json`, import.meta.url), "utf8"));
const registry = { descriptors: load("descriptors"), trustedLocks: [
  { pluginId: "org.example.pipeline", package: "pipeline-plugin", version: "1.2.0", sha256: "a".repeat(64), pluginApiVersion: "1.0-draft" },
  { pluginId: "org.example.math", package: "math-plugin", version: "2.0.0", sha256: "b".repeat(64), pluginApiVersion: "1.0-draft" },
] };
const text = () => readFileSync(new URL("../../tests/fixtures/plugin-descriptors/blueprint.json", import.meta.url), "utf8");
const directories: string[] = [];
afterEach(() => { for (const dir of directories.splice(0)) rmSync(dir, {recursive: true, force: true}); });
const directory = () => { const dir = mkdtempSync(join(tmpdir(), "v2-doc-")); directories.push(dir); writeFileSync(join(dir,"blueprint.json"), text()); return dir; };
const request = (operation: string, payload: object = {}, declarations = registry) => handleBlueprintDocumentRequest({protocolVersion: 2, requestId:"document-1", operation, registry: declarations, ...payload});
const hash = (reply: Awaited<ReturnType<typeof request>>) => "sha256" in reply.result ? reply.result.sha256 : "";
it("loads an approved baseline, previews without disk writes, saves and loads a new baseline", async () => {
  const dir = directory(); const loaded = await request("load", {directory:dir});
  expect(loaded.result).toMatchObject({status:"loaded", sha256:expect.stringMatching(/^[a-f0-9]{64}$/)});
  expect(loaded).toMatchObject({canGenerate:false, writable:true, editTargets:{projectName:"Reading pipeline",nodes:[expect.objectContaining({sourceIndex:0,properties:[{property:"label",valueLiteral:'"温度"'}],ports:[{portIndex:0,portId:"out",value:"可编辑显示名"}]}),expect.anything(),expect.anything()]}});
  const edits = [{kind:"project-name",value:"changed"},{kind:"node-property",sourceIndex:0,property:"label",valueLiteral:'"new"'},{kind:"port-name",sourceIndex:0,portIndex:0,portId:"out",value:"output"}];
  const preview = await request("preview", {sourceText:text(),edits});
  expect(preview.result.status).toBe("prepared"); expect(readFileSync(join(dir,"blueprint.json"),"utf8")).toBe(text());
  const saved = await request("save", {directory:dir,sourceText:text(),expectedSha256:hash(loaded),edits});
  expect(saved.result).toMatchObject({status:"saved",writeState:"verified"});
  expect(readFileSync(join(dir,"blueprint.json"),"utf8")).toBe(text().replace('"Reading pipeline"','"changed"').replace('"温度"','"new"').replace('"可编辑显示名"','"output"'));
  expect(hash(await request("load",{directory:dir}))).toBe(hash(saved));
}, 30000);
it("keeps policy rejected directories viewable with no writable baseline", async () => {
  const dir=directory();writeFileSync(join(dir,"README.md"),"protected");const reply=await request("load",{directory:dir});
  expect(reply).toMatchObject({writable:false,readOnlyReason:"DIRECTORY_UNSUITABLE",readResult:{status:"read"}});expect(reply.result).not.toHaveProperty("sha256");
}, 15000);
it("projects zero-plugin targets and rejects unknown internal edits", async () => {
  const empty={descriptors:[],trustedLocks:[]};const reply=await request("load",{directory:directory()},empty);
  expect(reply).toMatchObject({editTargets:{projectName:"Reading pipeline",nodes:[]}});
  expect((await request("preview",{sourceText:text(),edits:[{kind:"node-property",sourceIndex:0,property:"label",valueLiteral:'"x"'}]},empty)).result).toMatchObject({status:"failed",code:"TARGET_UNAVAILABLE"});
},15000);
it.each(["{}","[]","1e999","NaN",'"unterminated']) ("rejects unsupported scalar literal %s",async valueLiteral=>{
  expect((await request("preview",{sourceText:text(),edits:[{kind:"node-property",sourceIndex:0,property:"label",valueLiteral}]})).result.status).toBe("failed");
});
it("rejects contract violations and external conflicts without touching bytes", async()=>{
 const dir=directory();const loaded=await request("load",{directory:dir});
 expect((await request("preview",{sourceText:text(),edits:[{kind:"node-property",sourceIndex:0,property:"label",valueLiteral:"17"}]})).result).toMatchObject({code:"PROPERTY_CONTRACT"});
 writeFileSync(join(dir,"blueprint.json"),text()+"\n");
 expect((await request("save",{directory:dir,sourceText:text(),expectedSha256:hash(loaded),edits:[{kind:"project-name",value:"other"}]})).result).toMatchObject({code:"CONFLICT",writeState:"not-written"});
 expect(readFileSync(join(dir,"blueprint.json"),"utf8")).toBe(text()+"\n");
},30000);
it("preserves CRLF, escaped property names, opaque fragments, duplicate IDs and negative zero",async()=>{
 const descriptors=load("descriptors"),doc=load("blueprint");descriptors[0].nodeDefinitions[0].propertiesSchema.properties.amount={type:"number"};
 doc.nodes.push(structuredClone(doc.nodes[0]));doc.nodes[3].properties.amount=1;doc.opaque={text:"keep",array:[1,null,{x:true}]};
 const sourceText=JSON.stringify(doc,null,2).replaceAll("\n","\r\n").replace('"label": "温度"','"la\\u0062el": "温度"').replace('"keep"','"ke\\u0065p"');
 const edits=[{kind:"node-property",sourceIndex:3,property:"amount",valueLiteral:"-0"},{kind:"node-property",sourceIndex:0,property:"label",valueLiteral:'"new"'}];
 const reply=await request("preview",{sourceText,edits},{...registry,descriptors});
 expect(reply.result.status).toBe("prepared");if(reply.result.status!=="prepared")throw Error(JSON.stringify(reply));
 expect(reply.result.sourceText).toBe(sourceText.replace('"amount": 1','"amount": -0').replace('"la\\u0062el": "温度"','"la\\u0062el": "new"'));
 expect(reply.editTargets?.nodes.find(n=>n.sourceIndex===3)?.properties).toContainEqual({property:"amount",valueLiteral:"-0"});
 expect(Object.is((reply.result.readResult.document.nodes[3].properties as any).amount,-0)).toBe(true);
});
it("exposes bad existing declared scalars for repair while omitting nested and undeclared members",async()=>{
 const dir=directory(),doc=load("blueprint");doc.nodes[0].properties={label:17,nested:{x:1},undeclared:"opaque"};writeFileSync(join(dir,"blueprint.json"),JSON.stringify(doc));
 const loaded=await request("load",{directory:dir});expect(loaded.editTargets?.nodes[0].properties).toEqual([{property:"label",valueLiteral:"17"}]);
 const fixed=await request("preview",{sourceText:JSON.stringify(doc),edits:[{kind:"node-property",sourceIndex:0,property:"label",valueLiteral:'"repaired"'}]});
 expect(fixed.result.status).toBe("prepared");
},15000);
it.each(["missing","untrusted"])("never authorizes internal targets with %s plugins",async mode=>{
 const declarations=mode==="missing"?{descriptors:[],trustedLocks:[]}:{...registry,trustedLocks:[]};
 const loaded=await request("load",{directory:directory()},declarations);expect(loaded.editTargets?.nodes??[]).toEqual([]);
 expect((await request("preview",{sourceText:text(),edits:[{kind:"port-name",sourceIndex:0,portIndex:0,portId:"out",value:"x"}]},declarations)).result).toMatchObject({status:"failed",code:mode==="missing"?"TARGET_UNAVAILABLE":"REGISTRY_INVALID"});
},15000);
it("rejects duplicate port IDs, repeated targets, invalid envelopes, oversized source and unauthorized fields",async()=>{
 const doc=load("blueprint");doc.nodes[0].ports.push(doc.nodes[0].ports[0]);
 expect((await request("preview",{sourceText:JSON.stringify(doc),edits:[{kind:"port-name",sourceIndex:0,portIndex:0,portId:"out",value:"x"}]})).result).toMatchObject({code:"TARGET_UNAVAILABLE"});
 expect((await request("preview",{sourceText:text(),edits:[{kind:"project-name",value:"a"},{kind:"project-name",value:"b"}]})).result).toMatchObject({code:"OVERLAPPING_EDITS"});
 for(const input of [null,{}, {protocolVersion:2,requestId:"x",operation:"remove",registry}, {protocolVersion:2,requestId:"x",operation:"load",directory:"relative",registry}])expect((await handleBlueprintDocumentRequest(input)).result.status).toBe("failed");
 expect((await request("preview",{sourceText:" ".repeat(8*1024*1024+1),edits:[]})).result).toMatchObject({code:"SOURCE_TOO_LARGE"});
 expect((await request("preview",{sourceText:text(),edits:[{kind:"node-property",sourceIndex:0,property:"missing",valueLiteral:"null"}]})).result).toMatchObject({code:"TARGET_UNAVAILABLE"});
});
it("bounds the UTF-8 candidate before preview or saving rather than writing an oversized reply",async()=>{
 const reply=await request("preview",{sourceText:text(),edits:[{kind:"project-name",value:"值".repeat(3*1024*1024)}]});
 expect(reply.result).toMatchObject({status:"failed",code:"SOURCE_TOO_LARGE",writeState:"not-written"});
},15000);
