---
description: Start the host dev server, capture screenshots of affected workspaces, and run a quick Uncodixfy critique via the bsl-uncodixfy-reviewer sub-agent. Leaves screenshot paths in .agent/runs/powered-ready-console/Status.md for downstream /audit-gui or /review-loop.
---

## Step 1 — Confirm Launch Config

Check `.claude/launch.json` for a host dev server entry. If missing, create it with:

    {
      "version": "0.0.1",
      "configurations": [
        {
          "name": "bsl-host-console",
          "runtimeExecutable": "npm",
          "runtimeArgs": ["run", "dev", "--prefix", "host-console", "--", "--host", "127.0.0.1", "--port", "4173"],
          "port": 4173
        }
      ]
    }

## Step 2 — Start The Preview Server

Use `mcp__Claude_Preview__preview_start` with `name: "bsl-host-console"`. If the preview MCP is unavailable, fall back to running the dev server via Bash and instructing the user to open `http://127.0.0.1:4173/` manually — but prefer the MCP path.

## Step 3 — Capture Screenshots Per Workspace

For every workspace affected by the current diff (System / Operate / Integrate / Update / History):

1. Navigate (via `preview_eval` with `window.location.hash = '#/<workspace>'` or the app's routing equivalent).
2. Capture a screenshot with `mcp__Claude_Preview__preview_screenshot`.
3. Save the path (e.g. via the auto-returned `imageId` or an explicit save to `/tmp/render-<workspace>-<hash>.png`).

If the change touches the Operate deployment flow, also capture the deployment-running state mid-run so checklist rows show `PENDING / IN PROGRESS / PASSED` simultaneously.

## Step 4 — Spawn bsl-uncodixfy-reviewer

Use the `Agent` tool with `subagent_type: "bsl-uncodixfy-reviewer"` and pass the screenshot paths. It returns PASS/FAIL plus per-violation findings.

## Step 5 — Write Evidence Into Status.md

Under `Validation → Rendered verification` in `.agent/runs/powered-ready-console/Status.md`:

- Screenshot paths
- Uncodixfy verdict + any violations
- Whether the dev server ran cleanly

## Step 6 — Stop The Preview Server

Use `mcp__Claude_Preview__preview_stop` with the serverId you started.

## Do Not

- Do NOT accept GUI claims from static code review. Render the page.
- Do NOT skip capturing a deployment-running state if Operate changed. Idle-only screenshots miss the wizard progression.
- Do NOT bypass Uncodixfy. A1's verdict is authoritative for design-language compliance.
