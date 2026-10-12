import { readFileSync } from "node:fs";
import { Ajv2020, type AnySchemaObject } from "ajv/dist/2020.js";

// The machine schema is the only structural model. Inputs remain unknown.
export const schema: AnySchemaObject = JSON.parse(
  readFileSync(new URL("../../schemas/blueprint-v2.schema.json", import.meta.url), "utf8"),
);

export function createContracts() {
  const ajv = new Ajv2020({
    strict: true,
    allErrors: true,
    coerceTypes: false,
    removeAdditional: false,
    useDefaults: false,
  });
  ajv.addSchema(schema, "blueprint-v2");
  const validateBlueprint = ajv.getSchema<unknown>("blueprint-v2");
  if (!validateBlueprint) throw new Error("Blueprint v2 schema did not compile");
  const validateChannel = ajv.compile<unknown>({ $ref: "blueprint-v2#/$defs/Channel" });
  return { ajv, validateBlueprint, validateChannel };
}
