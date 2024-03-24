#include "SDL.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "filemap.h"
#include "filetree.h"
#include "imgui.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

constexpr int NUM_COLOURS = 12;
using Palette = SDL_Colour[NUM_COLOURS];

static Palette default_palette{
    {0xaf, 0x00, 0x00, 0x00}, {0xce, 0x5e, 0x13, 0x00},
    {0x32, 0x69, 0x10, 0x00}, {0x00, 0x81, 0xdd, 0x00},
    {0x00, 0x02, 0x93, 0x00}, {0xe9, 0x25, 0x8b, 0x00},
    {0xff, 0x9d, 0x00, 0x00}, {0xff, 0xdf, 0x52, 0x00},
    {0x8a, 0xd1, 0x18, 0x00}, {0x53, 0xe4, 0xf7, 0x00},
    {0x98, 0x1c, 0xe0, 0x00}, {0xff, 0x74, 0xc5, 0x00},
};

std::vector<SDL_FRect> MakeRects(const FileTree &tree, SDL_FRect space) {
  std::vector<SDL_FRect> rects = {space};

  for (node_index_t rect = 0; rect < tree.Size(); ++rect) {
    if (tree.GetFile(rect).type != File::DIRECTORY) {
      continue;
    }

    RowLayoutManager row_man(rects[rect], tree.GetFile(rect).size, rects);

    const node_index_t c0 = tree.GetFile(rect).first_child;
    if (c0 == NULL_INDEX) {
      continue;
    }
    // if c0 != NULL_INDEX then CountChildren >= 1 is guaranteed
    const node_index_t c1 = c0 + tree.CountChildren(rect) - 1;
    for (node_index_t i = c0; i <= c1; ++i) {
      row_man.Add(tree.GetFile(i).size);
    }
  }

  return rects;
}

class App {
public:
  App(const char *name, int width, int height);

  void SetTarget(FileTree *, SDL_FRect *);
  void SetPalette(Palette);

  void Run();
  bool IsRunning() const { return m_alive; }
  void Quit() { m_alive = false; }

private:
  void ProcessEvents();

  void UpdateMapTexture();
  void HighlightRect(node_index_t);

public:
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *screen;

  SDL_Colour clear_colour;

private:
  bool m_alive;

  FileTree *m_tree;
  SDL_FRect *m_rects;

  float m_zoom;
  SDL_FPoint m_offset;
  Palette m_palette;

  // for mouse over logic
  const int m_selected_rect_thickness = 3;
  node_index_t m_selected = 0;
  int m_selected_parent_depth = 0;
};

App::App(const char *name, int width, int height)
    : window(SDL_CreateWindow(name, 10, 30, width, height, 0)),
      renderer(SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC)),
      screen(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                               SDL_TEXTUREACCESS_TARGET, width, height)),
      clear_colour({0, 0, 0, 0}),

      m_alive(true),

      m_tree(nullptr), m_rects(nullptr),

      m_zoom(1), m_offset{0, 0}, m_palette(),

      m_selected(0), m_selected_parent_depth(0) {
  if (window == nullptr) {
    printf("Unable to create window: %s\n", SDL_GetError());
    assert(0);
  }
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    printf("Unable to initialize SDL: %s\n", SDL_GetError());
    assert(0);
  }
  if (renderer == nullptr) {
    printf("Unable to create renderer: %s\n", SDL_GetError());
    assert(0);
  }
  if (screen == nullptr) {
    printf("Unable to create screen texture: %s\n", SDL_GetError());
    assert(0);
  }

  // Setup ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer2_Init(renderer);

  SetPalette(default_palette);
}

void App::SetPalette(Palette p) {
  for (int i = 0; i < NUM_COLOURS; ++i) {
    m_palette[i] = p[i];
  }
}

void App::SetTarget(FileTree *tree, SDL_FRect *rects) {
  m_tree = tree;
  m_rects = rects;
}

void App::Run() {
  UpdateMapTexture();

  while (m_alive) {
    ProcessEvents();

    node_index_t ancestor;
    if (m_selected) {
      ancestor = m_selected;
      int i;
      for (i = 0; i < m_selected_parent_depth; ++i) {
        if (m_tree->GetFile(ancestor).parent == NULL_INDEX) {
          break;
        }
        ancestor = m_tree->GetFile(ancestor).parent;
      }
      m_selected_parent_depth = i;
    }

    // Start new drawing frame
    auto [r, g, b, a] = clear_colour;
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderClear(renderer);

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Draw to base 'screen' layer
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_Rect d = {(int)((1 - m_zoom) * w / 2 + m_offset.x),
                  (int)((1 - m_zoom) * h / 2 + m_offset.y), (int)(w * m_zoom),
                  (int)(h * m_zoom)};

    SDL_RenderCopy(renderer, screen, NULL, &d);

    // Draw on top of 'screen'
    if (m_selected) {
      HighlightRect(ancestor);
    }

    // Do all ImGui drawing
    if (m_selected) {
      const FileNode &anc = m_tree->GetFile(ancestor);
      fs::path p;
      if (anc.type != File::DIRECTORY and anc.parent != NULL_INDEX) {
        const FileNode &par = m_tree->GetFile(anc.parent);
        p = (par.path / anc.path);
      } else {
        p = anc.path;
      }

      {
        int w, h, x, y;
        SDL_GetWindowSize(window, &w, &h);
        SDL_GetMouseState(&x, &y);

        float ux =
            ImGui::CalcTextSize(p.string().c_str(), NULL, false, (float)(w - x))
                .x;

        int prefix = 0;
        double size = (double)anc.size;
        while (size > 1024) {
          size /= 1024;
          ++prefix;
        }
        assert(prefix < 7);
        char unit_prefix = " KMGTPE"[prefix];

        float px = ImGui::GetStyle().WindowPadding.x;
        ImGui::SetNextWindowSize({ux + 2 * px, 0.0f});
        ImGui::BeginTooltip();
        ImGui::TextWrapped("%s\n%.2f %cB\n", p.string().c_str(), size,
                           unit_prefix);
        ImGui::EndTooltip();
      }
    }

    // Present new frame
    ImGui::Render();
    ImGuiIO &io = ImGui::GetIO();
    SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x,
                       io.DisplayFramebufferScale.y);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(renderer);
  }
}

void App::ProcessEvents() {
  // Skip over events that ImGui wants to capture
  ImGuiIO &io = ImGui::GetIO();
  bool key_stolen, mouse_stolen;
  SDL_Event e;

  while (SDL_PollEvent(&e)) {
    ImGui_ImplSDL2_ProcessEvent(&e);
    key_stolen = (e.type == SDL_KEYDOWN or e.type == SDL_KEYUP) and
                 io.WantCaptureKeyboard;
    mouse_stolen = (e.type >= SDL_MOUSEMOTION and e.type <= SDL_MOUSEWHEEL) and
                   io.WantCaptureMouse;
    if (key_stolen or mouse_stolen) {
      continue;
    }

    // Event not used by ImGui, so we do App related stuff...
    switch (e.type) {
    case SDL_QUIT: {
      App::Quit();
    } break; // SDL_QUIT

    case SDL_KEYDOWN: {
      switch (e.key.keysym.sym) {
      case SDLK_ESCAPE: {
        App::Quit();
      } break; // SDLK_ESCAPE
      }

    } break; // SDL_KEYDOWN

    case SDL_MOUSEBUTTONDOWN: {
    } break; // SDL_MOUSEBUTTONDOWN

    case SDL_MOUSEBUTTONUP: {
      node_index_t ancestor;
      if (m_selected) {
        ancestor = m_selected;
        int i;
        for (i = 0; i < m_selected_parent_depth; ++i) {
          if (m_tree->GetFile(ancestor).parent == NULL_INDEX) {
            break;
          }
          ancestor = m_tree->GetFile(ancestor).parent;
        }
        m_selected_parent_depth = i;
      }
      const FileNode &fn = m_tree->GetFile(ancestor);
      fs::path p;
      if (!fn.parent or fn.type == File::DIRECTORY) {
        p = fn.path;
      } else {
        p = m_tree->GetFile(fn.parent).path / fn.path;
      }

      std::cout << '"' << std::string(p) << '"' << '\n';
    } break; // SDL_MOUSEBUTTONUP

    case SDL_MOUSEMOTION: {
      if (SDL_BUTTON_LMASK & e.motion.state) {
        m_offset.x += e.motion.xrel;
        m_offset.y += e.motion.yrel;

      } else {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        node_index_t new_selected = FindMouseClick(
            m_tree, m_rects,
            (e.motion.x - (1 - m_zoom) * w / 2 - m_offset.x) / m_zoom,
            (e.motion.y - (1 - m_zoom) * h / 2 - m_offset.y) / m_zoom);
        if (new_selected != m_selected) {
          m_selected_parent_depth = 0;
        }
        m_selected = new_selected;
      }
    } break; // SDL_MOUSEMOTION

    case SDL_MOUSEWHEEL: {
      if (SDL_BUTTON_LMASK & SDL_GetMouseState(nullptr, nullptr)) {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        float new_zoom = m_zoom * std::pow(0.9, -e.wheel.preciseY);
        new_zoom = std::clamp(new_zoom, 0.1f - m_zoom, 10.0f - m_zoom);
        float scale = (new_zoom - m_zoom) / m_zoom;
        m_offset.x += scale * m_offset.x;
        m_offset.y += scale * m_offset.y;

        m_zoom = new_zoom;
      } else {
        m_selected_parent_depth -= e.wheel.y;
        m_selected_parent_depth = std::max(0, m_selected_parent_depth);
      }
    } break; // SDL_MOUSEWHEEL
    }

    continue;
  }
}

void App::UpdateMapTexture() {
  SDL_SetRenderTarget(renderer, screen);

  for (node_index_t i = 0; i < m_tree->Size(); ++i) {
    SDL_Colour c = m_palette[i % NUM_COLOURS];
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRectF(renderer, &(m_rects[i]));
  }
  SDL_SetRenderTarget(renderer, NULL);
}

void App::HighlightRect(node_index_t r) {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  SDL_FRect outline = m_rects[r];

  int w, h;
  SDL_GetWindowSize(window, &w, &h);
  outline.x = outline.x * m_zoom + m_offset.x + (1 - m_zoom) * w / 2 - 1;
  outline.y = outline.y * m_zoom + m_offset.y + (1 - m_zoom) * h / 2 - 1;
  outline.w = outline.w * m_zoom + 2;
  outline.h = outline.h * m_zoom + 2;

  for (int i = 0; i < m_selected_rect_thickness; ++i) {
    SDL_RenderDrawRectF(renderer, &(outline));
    outline.x += 1;
    outline.y += 1;
    outline.w -= 2;
    outline.h -= 2;
  }
}
