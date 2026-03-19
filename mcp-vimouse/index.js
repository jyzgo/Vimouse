import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import net from "net";

const PIPE_PATH = "\\\\.\\pipe\\vimouse";

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

// --- Start Server ---

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
}

main().catch(console.error);
