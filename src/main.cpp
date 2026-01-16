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

import Core.Utilities;

import Editor.EditorLayer;

namespace
Engine {
    int Main(int argc, char **argv) {
        auto app = Engine::MakeRef<ImGuiApplication>();
        app->Init({
            .Title = "Frosty Engine App",
            .Width = 1920,
            .Height = 1080,
        });
        // app->EmplaceLayer<RendererDevelopmentLayer>();
        app->EmplaceLayer<Editor::EditorLayer>();
        app->EmplaceLayer<ImGuiDebugTestLayer>();
        app->Run();
        app->DetachAllLayers();
        app->Destroy();
        return 0;
    }
}
