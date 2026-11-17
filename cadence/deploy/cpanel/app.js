// Startup file for cPanel "Setup Node.js App" (Phusion Passenger).
// Passenger supplies PORT; the Next.js standalone server reads it.
process.env.NODE_ENV = "production";
process.env.HOSTNAME = process.env.HOSTNAME || "127.0.0.1";

// cPanel lets you set environment variables in its UI. As a fallback, read a
// .env file sitting next to this script (never inside public_html).
const fs = require("fs");
const path = require("path");
const envFile = path.join(__dirname, ".env");
if (fs.existsSync(envFile)) {
  for (const line of fs.readFileSync(envFile, "utf8").split(/\r?\n/)) {
    const m = /^\s*([A-Z0-9_]+)\s*=\s*(.*)\s*$/.exec(line);
    if (!m || line.trim().startsWith("#")) continue;
    const value = m[2].replace(/^(['"])(.*)\1$/, "$2");
    if (process.env[m[1]] === undefined) process.env[m[1]] = value;
  }
}

require("./server/server.js");
