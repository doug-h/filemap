#pragma once

#include "debug.h"
#include "filetree.h"

#include "SDL.h"

#include <cstdint>

// Take a FileTree and turn it into SDL_Rects for displaying

// A lot of this could be re-done more simply using relative sizes

struct Row {
  uintmax_t min_size;
  uintmax_t max_size;
  uintmax_t total_size = 0;

  std::vector<uintmax_t> m_elements;

  void Clear()
  {
    total_size = 0;
    m_elements.clear();
  }

  void Add(uintmax_t size)
  {
    if (size > 0) {
      if (total_size == 0) {
        min_size = max_size = total_size = size;
      } else {
        min_size = std::min(size, min_size);
        max_size = std::max(size, max_size);
        total_size += size;
      }
    }
    m_elements.push_back(size);
  }
};

struct Rect : SDL_FRect {
  uintmax_t size;
};

// The most 'squished' file rect possible if 'row' is placed in a 'space'
inline float GetWorstAspectRatio(const Row &row, const Rect &space)
{
  float a = (space.w > space.h) ? space.w : space.h;
  float b = (space.w > space.h) ? space.h : space.w;

  // TODO - avoid problems with large numbers
  float t = (float)(a * (float)row.total_size * (float)row.total_size) /
            (b * (float)space.size);
  return std::max(t / (float)row.min_size, (float)row.max_size / t);
}

// We have an unfinished row in a total space of total_size, does adding
// next_size to the row make the aspect ratio better than starting a new row
inline bool AddingReducesAspect(const Row &row, const Rect &space,
                                uintmax_t next_size)
{
  if (space.size == 0 or row.total_size == 0) { return true; }

  Row after = row;
  after.Add(next_size);
  return GetWorstAspectRatio(after, space) <= GetWorstAspectRatio(row, space);
}

// This class converts file sizes to rects and places them aesthetically
class RowLayoutManager {
public:
  // _parent_rect represents a directory of _parent_size
  RowLayoutManager(SDL_FRect _parent_rect, uintmax_t _parent_size,
                   std::vector<SDL_FRect> &out)
      : m_parent_rect{_parent_rect, _parent_size},
        m_remaining_rect(m_parent_rect), m_current_row(), m_out_rects(out)
  {}

  ~RowLayoutManager()
  {
    if (m_current_row.total_size != 0) { FinishRow(); }
  }

  void Add(uintmax_t size)
  {
    if (m_parent_rect.w < 1 or m_parent_rect.h < 1) {
      m_out_rects.push_back({0, 0, 0, 0});
      return;
    }
    if (!AddingReducesAspect(m_current_row, m_remaining_rect, size)) {
      FinishRow();
    }

    m_current_row.Add(size);
  }

private:
  void FinishRow()
  {
    if (m_current_row.total_size == 0) { return; }
    // End the temp row and place it.

    // If total_space is landscape (wider than tall) row takes a
    // horizontal split and stacks its contents vertically.
    // If total_space is portrait (taller than wide) row takes a vertical
    // split and stacks its contents horizontally.

    bool portrait = m_remaining_rect.h > m_remaining_rect.w;
    SDL_FRect row_space = m_remaining_rect;


    if (portrait) {
      float y_split = m_remaining_rect.h * (float)m_current_row.total_size /
                      (float)m_remaining_rect.size;
      row_space.h = y_split;
      m_remaining_rect.y += y_split;
      m_remaining_rect.h -= y_split;
      m_remaining_rect.size -= m_current_row.total_size;

      for (uintmax_t size : m_current_row.m_elements) {
        if (size == 0) {
          m_out_rects.push_back({0, 0, 0, 0});
          continue;
        }

        SDL_FRect ele_rect = row_space;
        ele_rect.w *= (float)size / (float)m_current_row.total_size;
        m_out_rects.push_back(ele_rect);

        // If density is changing drastically we have a problem
        float d = (ele_rect.w * ele_rect.h) / (float)size;

        float p =
            (m_parent_rect.w * m_parent_rect.h) / (float)m_parent_rect.size;
        assert(0.8f * p < d and d < 1.2f * p);

        row_space.x += ele_rect.w;
        row_space.w -= ele_rect.w;
        m_current_row.total_size -= size;
      }

    } else {
      float x_split = m_remaining_rect.w * (float)m_current_row.total_size /
                      (float)m_remaining_rect.size;
      row_space.w = x_split;
      m_remaining_rect.x += x_split;
      m_remaining_rect.w -= x_split;
      m_remaining_rect.size -= m_current_row.total_size;

      for (uintmax_t size : m_current_row.m_elements) {
        if (size == 0) {
          m_out_rects.push_back({0, 0, 0, 0});
          continue;
        }
        SDL_FRect ele_rect = row_space;
        ele_rect.h *= (float)size / (float)m_current_row.total_size;
        m_out_rects.push_back(ele_rect);

        // If density is changing drastically we have a problem
        float d = (ele_rect.w * ele_rect.h) / (float)size;

        float p =
            (m_parent_rect.w * m_parent_rect.h) / (float)m_parent_rect.size;
        assert(0.8f * p < d and d < 1.2f * p);

        row_space.y += ele_rect.h;
        row_space.h -= ele_rect.h;
        m_current_row.total_size -= size;
      }
    }

    m_current_row.Clear();
  }

private:
  const Rect m_parent_rect;


  Rect m_remaining_rect;
  Row m_current_row;

  std::vector<SDL_FRect> &m_out_rects;
};

node_index_t FindMouseClick(const FileTree *tree, const SDL_FRect *rects, int x,
                            int y)
{
  const SDL_FPoint p = {(float)x, (float)y};

  if (tree->GetRoot().type != File::DIRECTORY) { return 0; }

  node_index_t i = tree->GetRoot().first_child;
  node_index_t n = tree->CountChildren(0);
  if (i >= tree->Size()) { return 0; }

  node_index_t tightest_rect = 0;
  while (n > 0) {
    if (SDL_PointInFRect(&p, &(rects[i]))) {
      tightest_rect = i;
      if (tree->GetFile(i).type != File::DIRECTORY) { return tightest_rect; }

      n = tree->CountChildren(i);
      i = tree->GetFile(i).first_child;

      if (i >= tree->Size()) { return tightest_rect; }
    } else {
      ++i;
      --n;
    }
  }
  return tightest_rect;
}