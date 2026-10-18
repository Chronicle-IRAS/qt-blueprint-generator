import type { TrustedSemanticContext } from "./semantic-validator.js";

// Independently authored trusted descriptions, never derived from the document under test.
export const trustedContext: TrustedSemanticContext = {
  pluginLocks: ["org.blueprint.qt", "org.blueprint.bridge.ipc", "org.blueprint.bridge.serial", "org.example.compute", "org.example.audit"].map(pluginId => ({
    pluginId, package: `${pluginId}-package`, version: "0.1.0-draft.1", sha256: "a".repeat(64), pluginApiVersion: "1.0-draft",
  })),
  targets: [
    { targetId: "org.blueprint.qt/widgets", pluginId: "org.blueprint.qt" },
    { targetId: "org.example.compute/worker", pluginId: "org.example.compute" },
  ],
  nodeDefinitions: [
    ...["start", "end", "logic-module", "ui-page"].map(name => ({ typeId: `org.blueprint.qt/${name}`, version: "1.0.0-draft.1", pluginId: "org.blueprint.qt", supportedTargets: ["org.blueprint.qt/widgets"] })),
    { typeId: "org.example.compute/calculate", version: "1.0.0-draft.1", pluginId: "org.example.compute", supportedTargets: ["org.example.compute/worker"] },
    { typeId: "org.example.audit/record", version: "1.0.0-draft.1", pluginId: "org.example.audit", supportedTargets: ["org.example.compute/worker"] },
  ],
  portTypes: ["org.blueprint.core/control-flow@1", "org.blueprint.qt/QString@1", "org.example.audit/Entry@3"],
  compatibility: [],
  bridges: ["ipc", "serial"].map(name => ({
    pluginId: `org.blueprint.bridge.${name}`, version: "0.1.0-draft.1",
    mappings: [{ sourceTargetId: "org.blueprint.qt/widgets", targetTargetId: "org.blueprint.qt/widgets", sourceTypeId: "org.blueprint.qt/QString@1", targetTypeId: "org.blueprint.qt/QString@1", wireTypeId: "org.blueprint.wire/message@1" }],
    requiredDriverCount: { source: name === "serial" ? 1 : 0, target: name === "serial" ? 1 : 0 },
  })),
  portCardinality: [],
};
