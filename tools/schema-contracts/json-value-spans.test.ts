import { expect, it } from "vitest";
import { indexJsonValueSpans, JSON_SPAN_LIMITS } from "./json-value-spans.js";

it("indexes decoded keys and exact scalar positions without searching field names", () => {
  const text = ' {"a\\u002fb~": ["😀 key \\"", {"__proto__": -0.0e+0}], "constructor": false} ';
  const root = indexJsonValueSpans(text);
  const number = root.members!.get("a/b~")!.items![1].members!.get("__proto__")!;
  expect(text.slice(number.start, number.end)).toBe("-0.0e+0");
  expect(root.members!.get("constructor")!.kind).toBe("scalar");
});

it.each(['{"x":1,"x":2}', '{"x":1,"\\u0078":2}', '{"nested":[{"__proto__":1,"__proto__":2}]}'])("rejects duplicate decoded members anywhere: %s", text => {
  expect(() => indexJsonValueSpans(text)).toThrow(expect.objectContaining({ code: "DUPLICATE_KEY" }));
});

it("walks 10,000 containers without recursion", () => {
  const text = "[".repeat(10_000) + "null" + "]".repeat(10_000);
  expect(indexJsonValueSpans(text).end).toBe(text.length);
});

it.each([
  () => '"' + "x".repeat(JSON_SPAN_LIMITS.sourceLength) + '"',
  () => "[".repeat(JSON_SPAN_LIMITS.depth + 1) + "0" + "]".repeat(JSON_SPAN_LIMITS.depth + 1),
  () => "[" + Array(JSON_SPAN_LIMITS.values).fill("0").join(",") + "]",
])("rejects explicit resource overflow with a structured error", text => {
  expect(() => indexJsonValueSpans(text())).toThrow(expect.objectContaining({ code: "RESOURCE_LIMIT" }));
});

it.each(["", "[", '{"x" 1}', "[0 1]", '"unterminated', '{} {}'])("terminates on malformed text: %s", text => {
  expect(() => indexJsonValueSpans(text)).toThrow(expect.objectContaining({ code: "JSON_PARSE_ERROR" }));
});
