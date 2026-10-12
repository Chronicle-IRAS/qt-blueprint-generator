import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    include: ["blueprint-v2.test.ts"],
    environment: "node",
    fileParallelism: false,
  },
});
