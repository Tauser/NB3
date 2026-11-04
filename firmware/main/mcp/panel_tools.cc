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
        "mandar um som pro cartão\"). The address and a 6-digit code also appear on the robot's "
        "own screen for a minute or more. The result gives the code: say it aloud digit by digit "
        "(\"quatro, sete, ...\"), slowly, and say it once more if asked; tell the user the "
        "address is on the screen and to type the code on the page. Never invent a code. The "
        "panel closes by itself after 5 minutes without use or when a new conversation begins. "
        "It needs the robot to be online.",
        PropertyList(), [&panel](const PropertyList&) -> ReturnValue {
            switch (panel.Open()) {
                case PanelServer::OpenResult::kOpened: {
                    std::string spaced;
                    for (char digit : panel.Code()) {
                        spaced += digit;
                        spaced += ' ';
                    }
                    return "Opened. The address is on the robot's screen. The code is: " + spaced +
                           "(say it digit by digit).";
                }
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
