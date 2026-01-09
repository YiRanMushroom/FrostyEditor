import Core.Prelude;
import Core.Entrance;
import Vendor.ApplicationAPI;
import "SDL3/SDL.h";
import Core.Application;
import ImGui.ImGuiApplication;
import Render.Color;
import ImGui.ImGui;
import Render.Image;
import Core.STLExtension;
import Core.FileSystem;
import ImGuiDebugLayer;
import RendererDevelopmentLayer;

namespace
Engine {
    int Main(int argc, char **argv) {
        auto app = std::make_shared<ImGuiApplication>();
        app->Init({
            .Title = "Frosty Engine App"
        });
        app->EmplaceLayer<RendererDevelopmentLayer>();
        // app->EmplaceLayer<ImGuiDebugTestLayer>();
        app->Run();
        app->DetachAllLayers();
        app->Destroy();
        return 0;
    }
}
