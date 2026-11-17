import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  // Every page is per-user and dynamic, so Cache Components is left off.
  turbopack: {
    rules: {
      "*.css": {
        loaders: ["@tailwindcss/turbopack"],
        as: "*.css",
      },
    },
  },
};

export default nextConfig;
