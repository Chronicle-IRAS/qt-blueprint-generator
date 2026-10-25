// One request, one reply. The transport intentionally has no path/command operation.
import { handleReaderRequest, MESSAGE_LIMIT, type ReaderReply } from "./blueprint-reader-protocol.js";
const error = (code: string): ReaderReply => ({ protocolVersion: 1, requestId: "", canGenerate: false, status: "error", diagnostics: [{ code, severity: "error", message: code, path: "/request" }] });
let size = 0;
const chunks: Buffer[] = [];
let replied = false;
function reply(value: ReaderReply) {
  if (replied) return;
  replied = true;
  let output = JSON.stringify(value);
  if (Buffer.byteLength(output) > MESSAGE_LIMIT) output = JSON.stringify({ ...error("REPLY_TOO_LARGE"), requestId: value.requestId });
  process.stdout.write(output + "\n");
}
process.stdin.on("data", (chunk: Buffer) => {
  size += chunk.length;
  if (size > MESSAGE_LIMIT) {
    reply(error("REQUEST_TOO_LARGE"));
    process.stdin.destroy();
  } else chunks.push(chunk);
});
process.stdin.on("end", () => {
  if (replied) return;
  try {
    const text = new TextDecoder("utf-8", { fatal: true }).decode(Buffer.concat(chunks));
    reply(handleReaderRequest(JSON.parse(text)));
  } catch { reply(error("REQUEST_INVALID")); }
});
process.stdin.on("error", () => reply(error("STDIN_ERROR")));
