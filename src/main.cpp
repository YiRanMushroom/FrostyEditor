import Core.Prelude;
import Core.Entrance;
import Vendor.ApplicationAPI;
import "SDL3/SDL.h";
import Core.Application;
import ImGui.ImGuiApplication;

namespace
Engine {
    int Main(int argc, char **argv) {
        ImGuiApplication app;
        app.Init({
            .Title = "Frosty Engine App"
        });
        app.Run();
        app.Destroy();
        return 0;
    }
}
