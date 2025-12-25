import { reactRouter } from "@react-router/dev/vite";
import tailwindcss from "@tailwindcss/vite";
import { defineConfig } from "vite";
import tsconfigPaths from "vite-tsconfig-paths";
import { execSync } from "child_process";

// Get Git version info at build time
function getGitVersion() {
  try {
    // Try to get tag and commit info
    return execSync("git describe --tags --always --dirty", { encoding: "utf-8" }).trim();
  } catch (e) {
    try {
      // Fallback to short commit hash
      return execSync("git rev-parse --short HEAD", { encoding: "utf-8" }).trim();
    } catch (e2) {
      return "dev";
    }
  }
}

export default defineConfig({
  plugins: [tailwindcss(), reactRouter(), tsconfigPaths()],
  define: {
    __APP_VERSION__: JSON.stringify(getGitVersion()),
  },
});
