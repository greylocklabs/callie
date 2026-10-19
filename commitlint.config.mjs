// The rules are core's; the scopes are Callie's own.
import { config } from "./core/tooling/commitlint.mjs";

export default config([
  "core", // libcalliecore: models, sync, storage
  "gui", // callie-gui C++ entry point
  "qml", // QML views and components
  "theme", // the design system
  "cli", // the callie command-line interface
  "google", // Google Calendar backend
  "caldav", // CalDAV backend
  "packaging", // rpm spec, debian/, repo metadata
  "data", // desktop entry, AppStream, icons
  "site", // callieapp.org under www/
  "ci",
  "deps",
  "deps-dev", // Dependabot's updates to development dependencies
]);
