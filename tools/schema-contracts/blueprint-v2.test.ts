import { readFileSync, readdirSync } from "node:fs";
import { describe, expect, test, vi } from "vitest";
import { createContracts, schema } from "./validator.js";

const fixtureRoot = new URL("../../tests/fixtures/blueprint-v2/", import.meta.url);
const invalidCases = [
  ["root-required", "required", ""],
  ["root-type", "type", ""],
  ["schema-version-const", "const", "/schemaVersion"],
  ["schema-version-type", "type", "/schemaVersion"],
  ["empty-project-id", "minLength", "/projectId"],
  ["root-array-type", "type", "/units"],
  ["extensions-type", "type", "/extensions"],
  ["plugin-lock-required", "required", "/pluginLocks/0"],
  ["plugin-lock-type", "type", "/pluginLocks/0"],
  ["build-unit-required", "required", "/units/0"],
  ["build-unit-type", "type", "/units/0"],
  ["node-required", "required", "/nodes/0"],
  ["node-type", "type", "/nodes/0"],
  ["edge-required", "required", "/edges/0"],
  ["edge-type", "type", "/edges/0"],
  ["channel-required", "required", "/channels/0"],
  ["channel-type", "type", "/channels/0"],
  ["driver-binding-required", "required", "/driverBindings/0"],
  ["driver-binding-type", "type", "/driverBindings/0"],
  ["port-required", "required", "/nodes/0/ports/0"],
  ["port-type", "type", "/nodes/0/ports/0"],
  ["edge-endpoint-required", "required", "/edges/0/source"],
  ["edge-endpoint-type", "type", "/edges/0/source"],
  ["channel-endpoint-required", "required", "/channels/0/target"],
  ["channel-endpoint-type", "type", "/channels/0/target"],
  ["wire-schema-required", "required", "/channels/0/wireSchema"],
  ["wire-schema-type", "type", "/channels/0/wireSchema"],
  ["bridge-required", "required", "/channels/0/bridge"],
  ["bridge-type", "type", "/channels/0/bridge"],
  ["driver-file-required", "required", "/driverBindings/0/files/0"],
  ["driver-file-type", "type", "/driverBindings/0/files/0"],
  ["port-direction-enum", "enum", "/nodes/0/ports/0/direction"],
  ["port-kind-enum", "enum", "/nodes/0/ports/0/kind"],
  ["edge-kind-enum", "enum", "/edges/0/kind"],
  ["driver-mode-enum", "enum", "/driverBindings/0/mode"],
  ["plugin-hash-length", "minLength", "/pluginLocks/0/sha256"],
  ["plugin-hash-uppercase", "pattern", "/pluginLocks/0/sha256"],
  ["driver-file-hash", "pattern", "/driverBindings/0/files/0/sha256"],
  ["node-type-namespace", "pattern", "/nodes/0/typeId"],
  ["target-namespace", "pattern", "/units/0/targetId"],
  ["namespace-trailing-newline", "pattern", "/units/0/targetId"],
  ["port-type-namespace", "pattern", "/nodes/0/ports/0/typeId"],
  ["wire-type-namespace", "pattern", "/channels/0/wireSchema/typeId"],
  ["driver-id-list-type", "type", "/channels/0/bridge/endpointDriverBindingIds/source"],
  ["driver-id-list-item-type", "type", "/channels/0/bridge/endpointDriverBindingIds/source/0"],
  ["driver-id-list-duplicate", "uniqueItems", "/channels/0/bridge/endpointDriverBindingIds/target"],
  ["endpoint-driver-container-type", "type", "/channels/0/bridge/endpointDriverBindingIds"],
  ["empty-version", "minLength", "/channels/0/bridge/version"],
] as const;
const validNames = [
  "driver-free-bridge", "empty-structural-document", "multi-plugin-multi-unit",
  "opaque-json-values", "single-qt", "two-independent-driver-bindings",
];
const semanticNames = [
  "dangling-endpoint", "driver-wrong-endpoint-unit", "duplicate-node-id",
  "required-driver-missing", "same-target-cross-unit-edge",
];
function fixture(category: string, name: string): unknown {
  return JSON.parse(readFileSync(new URL(category + "/" + name + ".json", fixtureRoot), "utf8"));
}
function record(value: unknown): Record<string, unknown> {
  if (typeof value !== "object" || value === null || Array.isArray(value)) throw new Error("Expected test fixture object");
  return value as Record<string, unknown>;
}
function first(value: unknown): Record<string, unknown> {
  if (!Array.isArray(value)) throw new Error("Expected test fixture array");
  return record(value[0]);
}
function deepFreeze(value: unknown): void {
  if (typeof value === "object" && value !== null) {
    Object.values(value).forEach(deepFreeze);
    Object.freeze(value);
  }
}
const { ajv, validateBlueprint, validateChannel } = createContracts();

describe("Blueprint v2 structural contract", () => {
  test("explicit draft 2020-12 schema passes the built-in meta-schema and compiles synchronously", () => {
    expect(schema.$schema).toBe("https://json-schema.org/draft/2020-12/schema");
    expect(ajv.validateSchema(schema)).toBe(true);
    expect(validateBlueprint).not.toHaveProperty("$async");
    expect(validateChannel).not.toHaveProperty("$async");
    expect(ajv.opts.strictSchema).toBe(true);
    expect(ajv.opts.allErrors).toBe(true);
    expect(ajv.opts.coerceTypes).toBe(false);
    expect(ajv.opts.removeAdditional).toBe(false);
    expect(ajv.opts.useDefaults).toBe(false);
    expect(ajv.opts.loadSchema).toBeUndefined();
  });

  test("all three fixture collections are nonempty and every file is enumerated", () => {
    const expected = {
      valid: validNames,
      invalid: invalidCases.map(([name]) => name),
      "semantic-only": semanticNames,
    };
    for (const [category, names] of Object.entries(expected)) {
      const files = readdirSync(new URL(category + "/", fixtureRoot));
      expect(files.length).toBeGreaterThan(0);
      expect(files.every(name => name.endsWith(".json"))).toBe(true);
      expect(files.sort()).toEqual(names.map(name => name + ".json").sort());
    }
  });

  test.each(validNames)("accepts structural positive %s without mutating any data", name => {
    const data = fixture("valid", name);
    const before = structuredClone(data);
    const textBefore = JSON.stringify(data);
    deepFreeze(data);
    expect(validateBlueprint(data), JSON.stringify(validateBlueprint.errors)).toBe(true);
    expect(data).toEqual(before);
    expect(JSON.stringify(data)).toBe(textBefore);
  });

  test.each(invalidCases)("rejects %s at the intended %s location %s", (name, keyword, instancePath) => {
    const data = fixture("invalid", name);
    const before = structuredClone(data);
    deepFreeze(data);
    expect(validateBlueprint(data), "Invalid fixture was wrongly accepted: " + name).toBe(false);
    expect(validateBlueprint.errors).toEqual(expect.arrayContaining([
      expect.objectContaining({ keyword, instancePath }),
    ]));
    expect(data).toEqual(before);
  });

  test.each(semanticNames)("schema passes semantic counterexample %s; future semantic checker must reject it", name => {
    const data = fixture("semantic-only", name);
    const before = structuredClone(data);
    deepFreeze(data);
    expect(validateBlueprint(data), JSON.stringify(validateBlueprint.errors)).toBe(true);
    expect(data).toEqual(before);
  });

  test.each([undefined, {}, { source: [], target: [] }, { source: [] }, { target: [] }])(
    "permits optional or empty driver dependency declarations %j without adding defaults", value => {
      const data = record(fixture("valid", "driver-free-bridge"));
      const bridge = record(first(data.channels).bridge);
      if (value === undefined) delete bridge.endpointDriverBindingIds;
      else bridge.endpointDriverBindingIds = value;
      const before = structuredClone(data);
      deepFreeze(data);
      expect(validateBlueprint(data)).toBe(true);
      expect(data).toEqual(before);
    },
  );

  test.each([null, false, 7, "", [], {}])("accepts opaque JSON value %j in all plugin-owned payload fields", value => {
    const data = record(fixture("valid", "two-independent-driver-bindings"));
    first(data.units).settings = value;
    first(data.nodes).properties = value;
    record(first(data.channels).bridge).configuration = value;
    record(first(data.channels).wireSchema).schema = value;
    first(data.driverBindings).interfaceContract = value;
    const before = structuredClone(data);
    deepFreeze(data);
    expect(validateBlueprint(data), JSON.stringify(validateBlueprint.errors)).toBe(true);
    expect(data).toEqual(before);
  });

  test("allErrors reports independent failures without coercing version strings or filling missing fields", () => {
    const data = record(fixture("valid", "empty-structural-document"));
    data.schemaVersion = "2";
    delete data.projectId;
    const before = structuredClone(data);
    expect(validateBlueprint(data)).toBe(false);
    expect(validateBlueprint.errors).toEqual(expect.arrayContaining([
      expect.objectContaining({ keyword: "type", instancePath: "/schemaVersion" }),
      expect.objectContaining({ keyword: "required", instancePath: "", params: { missingProperty: "projectId" } }),
    ]));
    expect(data).toEqual(before);
  });

  test("only local $defs references are compiled; remote-looking payloads are never fetched", () => {
    const refs: string[] = [];
    function visit(value: unknown): void {
      if (typeof value !== "object" || value === null) return;
      const object = recordOrArray(value);
      if (typeof object.$ref === "string") refs.push(object.$ref);
      Object.values(value).forEach(visit);
    }
    function recordOrArray(value: object): { $ref?: unknown } {
      return value as { $ref?: unknown };
    }
    visit(schema);
    expect(refs.length).toBeGreaterThan(0);
    expect(refs.every(ref => ref.startsWith("#/$defs/"))).toBe(true);
    const fetch = vi.spyOn(globalThis, "fetch").mockImplementation(() => {
      throw new Error("Contract tests must not use the network");
    });
    try {
      const local = createContracts();
      const data = record(fixture("valid", "opaque-json-values"));
      record(first(data.channels).wireSchema).schema = {
        $schema: "https://offline.invalid/plugin-schema-dialect",
        $ref: "https://offline.invalid/opaque-wire-schema-data",
      };
      const before = structuredClone(data);
      deepFreeze(data);
      expect(local.validateBlueprint(data)).toBe(true);
      expect(data).toEqual(before);
      expect(() => local.ajv.compile({ $ref: "https://offline.invalid/not-a-local-schema" })).toThrow(/resolve reference/);
      expect(fetch).not.toHaveBeenCalled();
    } finally {
      fetch.mockRestore();
    }
  });

  test("complete Blueprint and partial Channel examples in all three Drafts use the same root/$defs validators", () => {
    const documents = ["blueprint-schema-v2", "plugin-api-v1", "plugin-platform"];
    let blueprintCount = 0;
    let channelCount = 0;
    for (const name of documents) {
      const markdown = readFileSync(new URL("../../doc/architecture/" + name + ".md", import.meta.url), "utf8");
      for (const match of markdown.matchAll(/\x60\x60\x60json\s*\n([\s\S]*?)\x60\x60\x60/g)) {
        const example: unknown = JSON.parse(match[1]);
        const object = record(example);
        if (object.schemaVersion === 2) {
          blueprintCount += 1;
          expect(validateBlueprint(example), name + ": " + JSON.stringify(validateBlueprint.errors)).toBe(true);
        } else if ("source" in object && "target" in object && "wireSchema" in object && "bridge" in object) {
          channelCount += 1;
          expect(validateChannel(example), name + ": " + JSON.stringify(validateChannel.errors)).toBe(true);
        }
      }
    }
    expect(blueprintCount).toBe(1);
    expect(channelCount).toBe(2);
  });
});
