#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

using node_index_t = uint32_t;
constexpr node_index_t NULL_INDEX = 0;

namespace fs = std::filesystem;

// TODO - get better values/make platform dependent
#define DIR_SIZE 4096
#define SYMLINK_SIZE 64

// ============ File ================
// All we need to know about a file
struct File {
  File(const fs::directory_entry &);
  ~File() = default;


  fs::path path;
  uintmax_t size;
  enum Type {
    REGULAR,
    DIRECTORY,
    SYMLINK,
    OTHER, // Anything we don't recognise gets Type == 'OTHER'
  } type;
};


// ============= FileNode =====================
// We store the tree as a flat array, nodes all store the index of their parent.
// Children are stored contiguously in the array, and parents store the index to
// the first child.
struct FileNode : File {
  FileNode(const File &_f, node_index_t _p) : File(_f), parent(_p) {}

  node_index_t parent;
  node_index_t first_child;
};


/* ============== FileTree =====================
 * A (flat)tree of files/directories.
 * To build the tree we create an array of nodes, initially just containing
 * root, then walk through the array. Whenever we see a directory we add its
 * children to the back of the array.
 * eg:
 *             vvv                       vvv
 * [root] -> [root,dir1,file1] -> [root,dir1,file1,file2]
 * represents
 *
 *                        root
 *                       /   \
 *                   dir1    file1
 *                    /
 *                 file2
 */

class FileTree {
public:
  FileTree(const fs::path &root);
  ~FileTree() {}

  // Expand the tree fully
  void Grow();

  // After the tree is fully expanded, we can calculate directory sizes as the
  // sum of their children
  void CalcSizes();

  int CountChildren(node_index_t directory) const;
  const FileNode &GetRoot() const { return m_nodes[0]; }
  const FileNode &GetFile(node_index_t i) const { return m_nodes[i]; }
  std::size_t Size() const { return (node_index_t)m_nodes.size(); }
  bool IsFullyGrown() const { return m_grow_index < m_nodes.size(); }

private:
  // Expand the next directory
  void GrowNext();

  // Move m_grow_index to the next directory
  void SkipToNextDir();


private:
  std::vector<FileNode> m_nodes;
  // Index of the next node that needs to be expanded
  node_index_t m_grow_index;
};

File::File(const fs::directory_entry &_f)
{
  // fs::status on a symlink returns the linked type e.g. 'directory', whereas
  // fs::symlink_status returns a specific type 'symlink'
  switch (_f.symlink_status().type()) {

  case fs::file_type::directory: {
    path = _f.path();
    size = DIR_SIZE;
    type = DIRECTORY;
  } break;

  case fs::file_type::regular: {
    path = _f.path().filename();
    size = _f.file_size();
    type = REGULAR;
  } break;

  case fs::file_type::symlink: {
    path = _f.path().filename();
    size = SYMLINK_SIZE;
    type = SYMLINK;
  } break;

  default: {
    path = _f.path().filename();
    type = OTHER;
    size = 0;
    std::clog << "Warning, unrecognised file: " << path << '\n';
  } break;
  }
}

FileTree::FileTree(const fs::path &_path) : m_grow_index(0)
{
  m_nodes.emplace_back(fs::directory_entry(_path), NULL_INDEX);
}

bool FileOrder(const FileNode &a, const FileNode& b){
  if(a.type == File::DIRECTORY){
    if(b.type == File::DIRECTORY){
      return a.path.filename() > b.path.filename();
    }
    return true;
  }
  if(b.type == File::DIRECTORY){
    return false;
  }

   return a.size > b.size; 
}

void FileTree::GrowNext()
{
  SkipToNextDir();

  if (m_grow_index >= m_nodes.size()) { return; }

  FileNode &f = m_nodes[m_grow_index];
  f.first_child = NULL_INDEX;

  const node_index_t child_slot = m_nodes.size();
  for (const fs::directory_entry &c : fs::directory_iterator(f.path)) {
    m_nodes.emplace_back(c, m_grow_index);

    // Only need to do this once
    m_nodes[m_grow_index].first_child = child_slot;
  }
  std::sort( m_nodes.begin() + child_slot, m_nodes.end(), FileOrder);

  ++m_grow_index;
}

void FileTree::Grow()
{
  std::cout << '\n';
  while (m_grow_index < m_nodes.size()) {
    GrowNext();
    if(Size() % 64 == 0){
      std::cout << "\x1B[2K\r" << Size() << " files" << std::flush;
    }
  }
  std::cout << "\x1B[2K\r\n";
}

void FileTree::SkipToNextDir()
{
  while (m_grow_index < m_nodes.size() and
         m_nodes[m_grow_index].type != File::DIRECTORY) {
    ++m_grow_index;
  }
}


void FileTree::CalcSizes()
{
  if (m_nodes.size() == 0) { return; }

  for (node_index_t i = m_nodes.size() - 1; i > 0; --i) {
    const FileNode &child = m_nodes[i];
    m_nodes[child.parent].size += child.size;
  }
}

int FileTree::CountChildren(node_index_t directory) const
{
  assert(m_nodes[directory].type == File::DIRECTORY);

  if (m_nodes[directory].first_child == NULL_INDEX) { return 0; }

  node_index_t start, end;
  start = m_nodes[directory].first_child;
  end = start + 1;

  while (end < m_nodes.size() and m_nodes[end].parent == directory) {
    ++end;
  }
  return end - start;
}
