#include <cassert>

#include "SDL.h"
#include "debug.h"
#include "filemap.h"
#include "filetree.h"
#include "window.h"

namespace fs = std::filesystem;

int main(int argv, char **args) {
  if (argv != 2) {
    puts("Usage filemap [directory]");
    return 0;
  }
  fs::path p(args[1]);

  FileTree master_tree(p);
  master_tree.Grow();
  master_tree.CalcSizes();

  std::vector<SDL_FRect> rects = MakeRects(master_tree, {0, 0, 900, 600});

  std::cout << master_tree.Size()
            << " files, total size: " << FormatSize{master_tree.GetRoot().size}
            << '\n';

  {
    App main_window("filemap", 900, 600);
    main_window.SetTarget(&master_tree, rects.data());

    main_window.Run();
  }
  return 0;
}
