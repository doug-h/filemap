#include <cassert>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <list>
#include <queue>
#include <stack>
#include <thread>
#include <vector>

#include "SDL.h"

// TODO - SWITCH multithreading based on HDD vs SSD, cores etc
// TODO - directory finite size based on filesystem

#define DIR_SIZE 4096

namespace fs = std::filesystem;

inline float lerp(float a, float b, float t) { return (1 - t) * a + t * b; }

struct FormatSize {
  std::uintmax_t s;

  friend std::ostream &operator<<(std::ostream &os, FormatSize f)
  {
    int unit = 0;
    float head = f.s;
    while (head >= 1024) {
      ++unit;
      head /= 1024;
    }
    head = std::ceil(head * 10.0f) / 10.0f;
    os << head;

    if (unit) { os << "KMGTPE"[unit - 1] << 'i'; }

    return os << 'B';
  }
};

std::uintmax_t decend(fs::path start)
{
  std::uintmax_t size = 0;
  for (const fs::directory_entry &dir_entry :
       fs::recursive_directory_iterator(start)) {
    if (dir_entry.is_regular_file()) { size += dir_entry.file_size(); }
  }

  return size;
}

template <typename F> class debug_task_wrapper {
  F f;

  // For debug
  std::chrono::time_point<std::chrono::high_resolution_clock> start;
  std::chrono::time_point<std::chrono::high_resolution_clock> end;

public:
  debug_task_wrapper(F f) : f(f)
  {
    start = std::chrono::high_resolution_clock::now();
  }
  ~debug_task_wrapper()
  {
    end = std::chrono::high_resolution_clock::now();
    std::cout << "Task took "
              << (std::chrono::duration_cast<std::chrono::microseconds>(end -
                                                                        start)
                      .count() *
                  1e-6)
              << "s." << '\n';
  }

  using return_type = typename decltype(std::function{f})::result_type;
  template <typename... Args> return_type operator()(Args... args)
  {
    std::cout << "Starting thread with " << (args << ...) << '\n';
    return f(args...);
  };
};


struct File {
  std::string name;
  uintmax_t size;
  SDL_Rect AABB;
};
struct Directory {
  fs::path path;
  uintmax_t size;
  SDL_Rect AABB;
  std::vector<File> files;
  std::vector<Directory *> children;
  Directory *parent;
};

void FillDirectory(Directory *dir)
{
  for (const fs::directory_entry &dir_entry :
       fs::directory_iterator(dir->path)) {
    if (dir_entry.is_regular_file()) {
      File f;
      f.size = dir_entry.file_size();
      f.name = dir_entry.path().filename();

      dir->files.push_back(f);

    } else if (dir_entry.is_directory() and !dir_entry.is_symlink()) {
      Directory *child = new Directory();
      child->path = dir_entry.path();
      child->parent = dir;
      dir->children.push_back(child);
    }
  }
}

void FillDirectoryRecursive(Directory *dir)
{
  FillDirectory(dir);
  for (Directory *c : dir->children) {
    FillDirectoryRecursive(c);
  }
}

void CalcSizes(Directory *root)
{
  root->size = DIR_SIZE;
  for (const auto &f : root->files) {
    root->size += f.size;
  }
  for (auto c : root->children) {
    CalcSizes(c);
    root->size += c->size;
  }
}

struct FileCountInfo {
  unsigned int n_files;
  unsigned int n_folders;
};

FileCountInfo CountFiles(Directory *root)
{
  FileCountInfo fc;
  fc.n_files = 0;
  fc.n_folders = 1;

  std::stack<Directory *> dq;
  dq.push(root);
  while (!dq.empty()) {
    Directory *d = dq.top();
    dq.pop();

    fc.n_files += d->files.size();
    for (const auto c : d->children) {
      ++fc.n_folders;
      dq.push(c);
    }
  }
  return fc;
}

// Spilts up the rect in root
void CreateRects(Directory *root)
{
  std::stack<Directory *> dq;
  dq.push(root);
  while (!dq.empty()) {
    Directory *d = dq.top();
    dq.pop();


    if (d->size == 0) {
      assert(d->children.size() == 0 and d->files.size() == 0);
    }
    float split = 1.0f / d->size;

    SDL_FRect remaining_space = {(float)d->AABB.x, (float)d->AABB.y,
                                 (float)d->AABB.w, (float)d->AABB.h};
    bool flipped = d->AABB.h > d->AABB.w;
    for (Directory *c : d->children) {
      c->AABB.x = remaining_space.x;
      c->AABB.y = remaining_space.y;
      float frac = c->size * split;
      if (flipped) {
        c->AABB.h = d->AABB.h * frac;
        c->AABB.w = remaining_space.w;
        remaining_space.y += d->AABB.h * frac;
        remaining_space.h -= d->AABB.h * frac;
      } else {
        c->AABB.w = d->AABB.w * frac;
        c->AABB.h = remaining_space.h;
        remaining_space.x += d->AABB.w * frac;
        remaining_space.w -= d->AABB.w * frac;
      }

      dq.push(c);
    }
    for (File &f : d->files) {
      f.AABB.x = remaining_space.x;
      f.AABB.y = remaining_space.y;
      float frac = f.size * split;
      if (flipped) {
        f.AABB.h = d->AABB.h * frac;
        f.AABB.w = remaining_space.w;
        remaining_space.y += d->AABB.h * frac;
        remaining_space.h -= d->AABB.h * frac;
      } else {
        f.AABB.w = d->AABB.w * frac;
        f.AABB.h = remaining_space.h;
        remaining_space.x += d->AABB.w * frac;
        remaining_space.w -= d->AABB.w * frac;
      }
    }
  }
}

void Render(SDL_Renderer *renderer, Directory *root, const SDL_Rect &start_rect,
            int colour_index = 0)
{
  constexpr SDL_Colour red{0xff, 0, 0, 0};
  constexpr SDL_Colour orange{0xff, 0xd6, 0xa5, 0};
  constexpr SDL_Colour yellow{0xfd, 0xff, 0xb6, 0};
  constexpr SDL_Colour green{0x00, 0xab, 0x55, 0};
  constexpr SDL_Colour blue{0x9b, 0xf6, 0xff, 0};
  constexpr SDL_Colour blue2{0xa0, 0xc4, 0xff, 0};
  constexpr SDL_Colour purple{0xbd, 0xb2, 0xff, 0};
  constexpr SDL_Colour pink{0xff, 0xc6, 0xff, 0};

  static const SDL_Colour palette[8] = {red,  orange, yellow, green,
                                        blue, blue2,  purple, pink};


  for (Directory *d : root->children) {
    Render(renderer, d, d->AABB, colour_index);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderDrawRect(renderer, &(d->AABB));
    colour_index = (colour_index + 1) % 8;
  }
  for (const File &f : root->files) {
    SDL_Colour c = palette[colour_index];
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(renderer, &(f.AABB));

    colour_index = (colour_index + 1) % 8;
  }
}

Directory *FindMouseClick(Directory *root, int x, int y)
{
  SDL_Point p = {x, y};
  assert(SDL_PointInRect(&p, &(root->AABB)));

  std::stack<Directory *> dq;
  dq.push(root);
  while (!dq.empty()) {
    Directory *d = dq.top();
    dq.pop();

    bool descend = false;
    for (Directory *c : d->children) {
      if (SDL_PointInRect(&p, &(c->AABB))) {
        dq.push(c);
        descend = true;
        break;
      }
    }
    if (!descend) {
      for (const File &f : d->files) {
        if (SDL_PointInRect(&p, &(f.AABB))) {
          std::cout << d->path / f.name << ' ' << FormatSize{f.size} << '\n';
          return d;
        }
      }
      std::cout << d->path << ' ' << FormatSize{d->size} << '\n';
      return d;
    }
  }
  return nullptr;
}


void Draw(fs::path p)
{
  SDL_Window *window = SDL_CreateWindow("filetree", 0, 0, 900, 600, 0);
  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);

  SDL_Rect screen = {0, 0, 900, 600};

  Directory root;
  root.path = p;
  root.AABB = screen;
  FillDirectoryRecursive(&root);
  CalcSizes(&root);

  CreateRects(&root);

  bool running = true;
  Directory *selected = nullptr;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) { running = false; }
      if (e.type == SDL_KEYDOWN and e.key.keysym.sym == SDLK_ESCAPE) {
        running = false;
      }
      if (e.type == SDL_MOUSEMOTION) {
        selected = FindMouseClick(&root, e.motion.x, e.motion.y);
      }
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    Render(renderer, &root, screen);

    if (selected) {
      SDL_SetRenderDrawColor(renderer, 255, 0, 255, 0);
      SDL_RenderDrawRect(renderer, &selected->AABB);
    }

    SDL_RenderPresent(renderer);
  }
}