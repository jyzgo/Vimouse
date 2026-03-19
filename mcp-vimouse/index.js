import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import net from "net";
import { readFileSync, writeFileSync } from "fs";
import { dirname, join } from "path";
import { fileURLToPath } from "url";

const __dirname = dirname(fileURLToPath(import.meta.url));
const PIPE_PATH = "\\\\.\\pipe\\vimouse";
const HUB_INDEX_PATH = join(__dirname, "hub-index.json");

// Send a command to Vimouse via Named Pipe and return the response
function sendCommand(command) {
  return new Promise((resolve, reject) => {
    const client = net.connect(PIPE_PATH, () => {
      client.write(command + "\n");
    });

    let data = "";
    client.on("data", (chunk) => {
      data += chunk.toString();
    });

    client.on("end", () => {
      const result = data.trim();
      if (result.startsWith("ERR")) {
        resolve({ success: false, error: result.substring(4) });
      } else if (result.startsWith("OK ")) {
        resolve({ success: true, data: result.substring(3) });
      } else if (result === "OK") {
        resolve({ success: true });
      } else {
        resolve({ success: true, data: result });
      }
    });

    client.on("error", (err) => {
      reject(new Error(`Cannot connect to Vimouse: ${err.message}. Is Vimouse running?`));
    });

    // Timeout after 30s
    client.setTimeout(30000, () => {
      client.destroy();
      reject(new Error("Command timed out"));
    });
  });
}

// Helper to format MCP tool response
function toolResult(result) {
  if (!result.success) {
    return { content: [{ type: "text", text: `Error: ${result.error}` }], isError: true };
  }
  return { content: [{ type: "text", text: result.data || "OK" }] };
}

const server = new McpServer({
  name: "vimouse",
  version: "1.0.0",
});

// --- Mouse Control Tools ---

server.tool("move", "Move mouse cursor to absolute screen coordinates", {
  x: z.number().describe("X coordinate"),
  y: z.number().describe("Y coordinate"),
}, async ({ x, y }) => {
  return toolResult(await sendCommand(`move ${x} ${y}`));
});

server.tool("click", "Left-click at coordinates (or current position if omitted)", {
  x: z.number().optional().describe("X coordinate (optional)"),
  y: z.number().optional().describe("Y coordinate (optional)"),
}, async ({ x, y }) => {
  const cmd = x !== undefined && y !== undefined ? `click ${x} ${y}` : "click";
  return toolResult(await sendCommand(cmd));
});

server.tool("rclick", "Right-click at coordinates (or current position)", {
  x: z.number().optional().describe("X coordinate (optional)"),
  y: z.number().optional().describe("Y coordinate (optional)"),
}, async ({ x, y }) => {
  const cmd = x !== undefined && y !== undefined ? `rclick ${x} ${y}` : "rclick";
  return toolResult(await sendCommand(cmd));
});

server.tool("dclick", "Double-click at coordinates (or current position)", {
  x: z.number().optional().describe("X coordinate (optional)"),
  y: z.number().optional().describe("Y coordinate (optional)"),
}, async ({ x, y }) => {
  const cmd = x !== undefined && y !== undefined ? `dclick ${x} ${y}` : "dclick";
  return toolResult(await sendCommand(cmd));
});

server.tool("drag", "Drag from one position to another", {
  x1: z.number().describe("Start X"),
  y1: z.number().describe("Start Y"),
  x2: z.number().describe("End X"),
  y2: z.number().describe("End Y"),
  duration_ms: z.number().optional().default(300).describe("Drag duration in ms"),
}, async ({ x1, y1, x2, y2, duration_ms }) => {
  return toolResult(await sendCommand(`drag ${x1} ${y1} ${x2} ${y2} ${duration_ms}`));
});

server.tool("scroll", "Scroll mouse wheel in a direction", {
  direction: z.enum(["up", "down", "left", "right"]).describe("Scroll direction"),
  amount: z.number().optional().default(3).describe("Number of scroll units"),
}, async ({ direction, amount }) => {
  return toolResult(await sendCommand(`scroll ${direction} ${amount}`));
});

server.tool("get_pos", "Get current mouse cursor position", {}, async () => {
  return toolResult(await sendCommand("pos"));
});

// --- Tag Tools ---

server.tool("get_tags", "List all screen position tags", {}, async () => {
  return toolResult(await sendCommand("tags"));
});

server.tool("jump_tag", "Jump cursor to a named tag position", {
  letter: z.string().length(1).describe("Tag letter (A-Z)"),
  click: z.boolean().optional().default(false).describe("Click after jumping"),
}, async ({ letter, click }) => {
  const cmd = click ? `tag ${letter} click` : `tag ${letter}`;
  return toolResult(await sendCommand(cmd));
});

// --- Window Query Tools ---

server.tool("get_active_window", "Get info about the currently active/foreground window", {}, async () => {
  return toolResult(await sendCommand("get_active_window"));
});

server.tool("list_windows", "List all visible windows with their titles, positions, and handles", {}, async () => {
  return toolResult(await sendCommand("list_windows"));
});

server.tool("find_window", "Find windows by title (case-insensitive substring match)", {
  title: z.string().describe("Title text to search for"),
}, async ({ title }) => {
  return toolResult(await sendCommand(`find_window ${title}`));
});

server.tool("wait_window", "Wait for a window with matching title to appear", {
  title: z.string().describe("Title text to search for"),
  timeout_ms: z.number().optional().default(30000).describe("Timeout in milliseconds"),
}, async ({ title, timeout_ms }) => {
  return toolResult(await sendCommand(`wait_window ${title} ${timeout_ms}`));
});

// --- UI Automation Tools ---

server.tool("find_element", "Find a UI element by name in the active window (uses Windows UI Automation)", {
  name: z.string().describe("Element name to search for"),
  type: z.string().optional().describe("Element type filter (Button, Edit, MenuItem, Tab, etc.)"),
}, async ({ name, type }) => {
  const cmd = type ? `find_element ${name} ${type}` : `find_element ${name}`;
  return toolResult(await sendCommand(cmd));
});

server.tool("list_elements", "List UI elements of a window (uses Windows UI Automation)", {
  hwnd: z.string().optional().describe("Window handle in hex (e.g. '0x1A2B'). Default: active window"),
  max_depth: z.number().optional().default(2).describe("Max depth to traverse (default 2)"),
}, async ({ hwnd, max_depth }) => {
  let cmd = "list_elements";
  if (hwnd) cmd += ` ${hwnd}`;
  if (max_depth !== undefined) {
    if (!hwnd) cmd += " 0"; // placeholder for active window
    cmd += ` ${max_depth}`;
  }
  return toolResult(await sendCommand(cmd));
});

server.tool("click_element", "Find and click a UI element by name (uses Invoke pattern or coordinate click)", {
  name: z.string().describe("Element name to click"),
  type: z.string().optional().describe("Element type filter (Button, MenuItem, etc.)"),
}, async ({ name, type }) => {
  const cmd = type ? `click_element ${name} ${type}` : `click_element ${name}`;
  return toolResult(await sendCommand(cmd));
});

// --- Screen OCR Tools ---

server.tool("scan_region", "OCR scan a screen region - returns recognized text with coordinates. Use this to 'see' what's on screen without screenshots.", {
  x1: z.number().describe("Left edge X"),
  y1: z.number().describe("Top edge Y"),
  x2: z.number().describe("Right edge X"),
  y2: z.number().describe("Bottom edge Y"),
}, async ({ x1, y1, x2, y2 }) => {
  return toolResult(await sendCommand(`scan_region ${x1} ${y1} ${x2} ${y2}`));
});

server.tool("read_at", "OCR read text near a screen position (default 300x60 area around the point)", {
  x: z.number().describe("Center X coordinate"),
  y: z.number().describe("Center Y coordinate"),
  width: z.number().optional().default(300).describe("Capture width"),
  height: z.number().optional().default(60).describe("Capture height"),
}, async ({ x, y, width, height }) => {
  return toolResult(await sendCommand(`read_at ${x} ${y} ${width} ${height}`));
});

// --- High-level Automation Tools ---

function loadHubIndex() {
  try {
    return JSON.parse(readFileSync(HUB_INDEX_PATH, "utf-8"));
  } catch {
    return null;
  }
}

function saveHubIndex(index) {
  writeFileSync(HUB_INDEX_PATH, JSON.stringify(index, null, 2), "utf-8");
}

server.tool("open_unity_project", "Open a Unity project via Unity Hub. Uses cached index for fast lookup, falls back to OCR scan if needed.", {
  project: z.string().describe("Project name or path keyword (e.g. 'EvonyPlus', 'RSProject', 'FoRelease')"),
  rescan: z.boolean().optional().default(false).describe("Force OCR rescan of Unity Hub"),
}, async ({ project, rescan }) => {
  const index = loadHubIndex();
  if (!index) {
    return { content: [{ type: "text", text: "Error: hub-index.json not found" }], isError: true };
  }

  // Find matching project in index
  const keyword = project.toLowerCase();
  let match = index.projects.find(p =>
    p.name.toLowerCase().includes(keyword) ||
    p.path.toLowerCase().includes(keyword)
  );

  // Step 1: Find or launch Unity Hub
  let hubResult = await sendCommand("find_window Unity Hub");
  let hubRect;

  if (!hubResult.success) {
    return { content: [{ type: "text", text: "Error: Unity Hub not running. Please start Unity Hub first." }], isError: true };
  }

  const hubWindows = JSON.parse(hubResult.data);
  const hub = hubWindows.find(w => w.title.includes("Unity Hub"));
  if (!hub) {
    return { content: [{ type: "text", text: "Error: Unity Hub window not found" }], isError: true };
  }
  hubRect = hub.rect; // [left, top, right, bottom]

  // Bring Hub to foreground
  const hubCenterX = Math.round((hubRect[0] + hubRect[2]) / 2);
  const hubCenterY = Math.round(hubRect[1] + 30);
  await sendCommand(`click ${hubCenterX} ${hubCenterY}`);
  await new Promise(r => setTimeout(r, 500));

  // Step 2: If rescan or no match, OCR scan the Hub
  if (rescan || !match) {
    const scanResult = await sendCommand(
      `scan_region ${hubRect[0]} ${hubRect[1]} ${hubRect[2]} ${hubRect[3]}`
    );
    if (scanResult.success && scanResult.data) {
      const ocrItems = JSON.parse(scanResult.data);
      // Try to find project name in OCR results
      const nameMatch = ocrItems.find(item =>
        item.text.toLowerCase().includes(keyword)
      );
      if (nameMatch) {
        // Click directly on the OCR match
        const clickX = Math.round((nameMatch.rect[0] + nameMatch.rect[2]) / 2);
        const clickY = Math.round((nameMatch.rect[1] + nameMatch.rect[3]) / 2);
        await sendCommand(`dclick ${clickX} ${clickY}`);

        // Update index with new position
        const offsetY = clickY - hubRect[1];
        const xRatio = (clickX - hubRect[0]) / (hubRect[2] - hubRect[0]);
        if (match) {
          match.click_offset = { x_ratio: xRatio, y_abs_from_top: offsetY };
          match.last_scanned = new Date().toISOString().split("T")[0];
        } else {
          // Add new project to index
          index.projects.push({
            name: nameMatch.text,
            path: "",
            editor_version: "",
            click_offset: { x_ratio: xRatio, y_abs_from_top: offsetY },
            row_index: index.projects.length,
            last_scanned: new Date().toISOString().split("T")[0],
          });
        }
        saveHubIndex(index);

        return { content: [{ type: "text", text: `Opened "${nameMatch.text}" (via OCR scan). Waiting for Unity Editor...` }] };
      }
      return { content: [{ type: "text", text: `Project "${project}" not found in Unity Hub. OCR results: ${scanResult.data.substring(0, 500)}` }], isError: true };
    }
  }

  if (!match) {
    return { content: [{ type: "text", text: `Project "${project}" not found in index. Try with rescan=true.` }], isError: true };
  }

  // Step 3: Click using cached position
  const clickX = Math.round(hubRect[0] + match.click_offset.x_ratio * (hubRect[2] - hubRect[0]));
  const clickY = Math.round(hubRect[1] + match.click_offset.y_abs_from_top);
  await sendCommand(`dclick ${clickX} ${clickY}`);

  return { content: [{ type: "text", text: `Opened "${match.name}" (${match.path}) via cached index. Click at (${clickX}, ${clickY}).` }] };
});

server.tool("list_unity_projects", "List Unity projects from the cached hub index", {}, async () => {
  const index = loadHubIndex();
  if (!index) {
    return { content: [{ type: "text", text: "Error: hub-index.json not found" }], isError: true };
  }
  const summary = index.projects.map(p => `- ${p.name}: ${p.path} (${p.editor_version})`).join("\n");
  return { content: [{ type: "text", text: summary }] };
});

server.tool("refresh_hub_index", "Re-scan Unity Hub with OCR and update the project index cache", {}, async () => {
  const index = loadHubIndex();
  if (!index) {
    return { content: [{ type: "text", text: "Error: hub-index.json not found" }], isError: true };
  }

  const hubResult = await sendCommand("find_window Unity Hub");
  if (!hubResult.success) {
    return { content: [{ type: "text", text: "Error: Unity Hub not running" }], isError: true };
  }
  const hubWindows = JSON.parse(hubResult.data);
  const hub = hubWindows.find(w => w.title.includes("Unity Hub"));
  if (!hub) {
    return { content: [{ type: "text", text: "Error: Unity Hub window not found" }], isError: true };
  }

  // Bring to front
  await sendCommand(`click ${Math.round((hub.rect[0] + hub.rect[2]) / 2)} ${hub.rect[1] + 30}`);
  await new Promise(r => setTimeout(r, 500));

  // OCR scan
  const scanResult = await sendCommand(
    `scan_region ${hub.rect[0]} ${hub.rect[1]} ${hub.rect[2]} ${hub.rect[3]}`
  );
  if (!scanResult.success) {
    return { content: [{ type: "text", text: "Error: OCR scan failed: " + scanResult.error }], isError: true };
  }

  return { content: [{ type: "text", text: `Hub scanned. OCR data:\n${scanResult.data}` }] };
});

// --- Start Server ---

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
}

main().catch(console.error);
