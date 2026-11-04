#include "panel_tools.h"

#include <string>

#include "mcp_server.h"

void RegisterPanelTools(PanelServer& panel) {
    McpServer& mcp = McpServer::GetInstance();
    mcp.AddTool(
        "self.panel.open",
        "Open the robot's settings panel, a web page the user opens in a browser on the same "
        "Wi-Fi to change the volume, the body LEDs and the sounds on the memory card, and to "
        "see the robot's stats. Use it when the user asks to open the panel, the settings or "
        "the control page (\"abre o painel\", \"quero ajustar o volume pelo celular\", \"quero "
        "mandar um som pro cartão\"). The address and a 6-digit code appear on the robot's own "
        "screen once you finish talking; NEVER say or guess the code, just tell the user to "
        "read the address and the code on the screen and to type the code on the page. The "
        "panel closes by itself after 5 minutes without use or when a new conversation begins. "
        "It needs the robot to be online.",
        PropertyList(), [&panel](const PropertyList&) -> ReturnValue {
            switch (panel.Open()) {
                case PanelServer::OpenResult::kOpened:
                    return std::string("Opened. The address and the code are on the robot's "
                                       "screen. Tell the user to read them there.");
                case PanelServer::OpenResult::kNoNetwork:
                    return std::string("Not opened: the robot has no Wi-Fi address right now.");
                default:
                    return std::string("Not opened: the panel could not start (low memory?).");
            }
        });
    mcp.AddTool(
        "self.panel.close",
        "Close the settings panel at once, for example when the user says they are done with it "
        "or asks to close or turn off the panel. Does nothing if it is not open.",
        PropertyList(), [&panel](const PropertyList&) -> ReturnValue {
            panel.Close();
            return true;
        });
}
