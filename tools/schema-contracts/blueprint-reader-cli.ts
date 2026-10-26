// One request, one reply. The transport intentionally has no path/command operation.
import { handleReaderRequest, MESSAGE_LIMIT, type ReaderReply } from "./blueprint-reader-protocol.js";
import { handleBlueprintDocumentRequest, type BlueprintDocumentReply } from "./blueprint-document-protocol.js";
const error = (code: string): ReaderReply => ({ protocolVersion: 1, requestId: "", canGenerate: false, status: "error", diagnostics: [{ code, severity: "error", message: code, path: "/request" }] });
let size = 0;
const chunks: Buffer[] = [];
let replied = false;
function reply(value: ReaderReply | BlueprintDocumentReply) {
  if (replied) return;
  replied = true;
  let output = JSON.stringify(value);
  if (Buffer.byteLength(output) > MESSAGE_LIMIT) output = JSON.stringify(value.protocolVersion === 2 ? {
    protocolVersion:2,requestId:value.requestId,operation:value.operation,canGenerate:false,
    result:{status:"failed",code:"REPLY_TOO_LARGE",message:"Reply exceeds limit; inspect and reload before saving",canGenerate:false,writeState:value.operation==="save"?"committed-unverified":"not-written"},
  } : { ...error("REPLY_TOO_LARGE"), requestId: value.requestId });
  process.stdout.write(output + "\n");
}
process.stdin.on("data", (chunk: Buffer) => {
  size += chunk.length;
  if (size > MESSAGE_LIMIT) {
    reply(error("REQUEST_TOO_LARGE"));
    process.stdin.destroy();
  } else chunks.push(chunk);
});
process.stdin.on("end", async () => {
  if (replied) return;
  try {
    const text = new TextDecoder("utf-8", { fatal: true }).decode(Buffer.concat(chunks));
    const input=JSON.parse(text);
    reply(input?.protocolVersion===2 ? await handleBlueprintDocumentRequest(input) : handleReaderRequest(input));
  } catch { reply(error("REQUEST_INVALID")); }
});
process.stdin.on("error", () => reply(error("STDIN_ERROR")));
