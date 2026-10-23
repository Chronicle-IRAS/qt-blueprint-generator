/** UTF-16 positions only. JSON.parse/Reader remain the syntax and structure authorities. */
export const JSON_SPAN_LIMITS = Object.freeze({ sourceLength: 8 * 1024 * 1024, values: 250_000, depth: 16_384 });
export interface JsonValueSpan {
  readonly start: number;
  end: number;
  readonly kind: "object" | "array" | "scalar";
  readonly members?: Map<string, JsonValueSpan>;
  readonly items?: JsonValueSpan[];
}
export class JsonSpanError extends Error {
  constructor(readonly code: "RESOURCE_LIMIT" | "DUPLICATE_KEY" | "JSON_PARSE_ERROR", message: string) { super(message); }
}
type Frame = { span: JsonValueSpan; state: "key" | "colon" | "value" | "comma"; key?: string };
const whitespace = (c: string) => c === " " || c === "\t" || c === "\r" || c === "\n";

/** Bounded, non-recursive scan; it never searches for a field name in source text. */
export function indexJsonValueSpans(text: string): JsonValueSpan {
  const invalid = (): never => { throw new JsonSpanError("JSON_PARSE_ERROR", "Invalid JSON text"); };
  if (text.length > JSON_SPAN_LIMITS.sourceLength) throw new JsonSpanError("RESOURCE_LIMIT", "JSON text exceeds the source length limit");
  let offset = 0, count = 0;
  const stack: Frame[] = [];
  const skip = () => { while (offset < text.length && whitespace(text[offset])) offset++; };
  const stringEnd = () => {
    if (text[offset++] !== '"') return invalid();
    while (offset < text.length) {
      const c = text[offset++];
      if (c === '"') return;
      if (c === "\\") offset++;
    }
    invalid();
  };
  const value = (): JsonValueSpan => {
    skip();
    const start = offset, c = text[offset];
    if (c === undefined) return invalid();
    if (++count > JSON_SPAN_LIMITS.values) throw new JsonSpanError("RESOURCE_LIMIT", "JSON text exceeds the indexed value limit");
    if (c === "{" || c === "[") {
      const span: JsonValueSpan = { start, end: -1, kind: c === "{" ? "object" : "array", ...(c === "{" ? { members: new Map() } : { items: [] }) };
      offset++;
      if (stack.length >= JSON_SPAN_LIMITS.depth) throw new JsonSpanError("RESOURCE_LIMIT", "JSON text exceeds the container depth limit");
      stack.push({ span, state: c === "{" ? "key" : "value" });
      return span;
    }
    if (c === '"') stringEnd();
    else {
      while (offset < text.length && !whitespace(text[offset]) && ![",", "]", "}"].includes(text[offset])) offset++;
      if (offset === start) return invalid();
    }
    return { start, end: offset, kind: "scalar" };
  };
  const root = value();
  while (stack.length) {
    skip();
    const frame = stack[stack.length - 1], { span } = frame;
    const close = span.kind === "object" ? "}" : "]";
    if (text[offset] === close && (frame.state === "key" || frame.state === "value" || frame.state === "comma")) {
      span.end = ++offset; stack.pop(); continue;
    }
    if (frame.state === "key") {
      const start = offset;
      stringEnd();
      let key: string;
      try { key = JSON.parse(text.slice(start, offset)) as string; } catch { return invalid(); }
      if (span.members!.has(key)) throw new JsonSpanError("DUPLICATE_KEY", "Duplicate decoded JSON object member");
      frame.key = key; frame.state = "colon";
    } else if (frame.state === "colon") {
      if (text[offset++] !== ":") return invalid();
      frame.state = "value";
    } else if (frame.state === "value") {
      frame.state = "comma";
      const child = value();
      if (span.members) span.members.set(frame.key!, child);
      else span.items!.push(child);
    } else {
      if (text[offset++] !== ",") return invalid();
      frame.state = span.kind === "object" ? "key" : "value";
    }
  }
  skip();
  if (offset !== text.length) return invalid();
  return root;
}
