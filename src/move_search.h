#pragma once

#include <vector>
#include <stdexcept>
#include <algorithm>

#include "game.h"
#include "board.h"
#include "position.h"
#include "constexpr_helpers.h"

class PossibleMoves {
  static void UniqueVector_(std::vector<Position>& p, bool unique) {
    std::sort(p.begin(), p.end());
    p.resize(std::unique(p.begin(), p.end()) - p.begin());
  }
 public:
  std::vector<Position> non_adj;
  std::vector<std::pair<Position, std::vector<Position>>> adj;
  void Normalize(bool unique = false) {
    UniqueVector_(non_adj, unique);
    for (auto& i : adj) UniqueVector_(i.second, unique);
    std::sort(adj.begin(), adj.end());
  }
};

namespace move_search {

constexpr int GetRow(int frame, Level level) {
  switch (level) {
    case kLevel18: return frame / 3;
    case kLevel19: return frame / 2;
    case kLevel29: return frame;
    case kLevel39: return frame * 2;
  }
  unreachable();
}

constexpr bool IsDropFrame(int frame, Level level) {
  switch (level) {
    case kLevel18: return frame % 3 == 2;
    case kLevel19: return frame % 2 == 1;
    default: return true;
  }
}

constexpr int NumDrops(int frame, Level level) {
  if (!IsDropFrame(frame, level)) return 0;
  switch (level) {
    case kLevel39: return 2;
    default: return 1;
  }
}

constexpr int GetFirstFrameOnRow(int row, Level level) {
  switch (level) {
    case kLevel18: return row * 3;
    case kLevel19: return row * 2;
    case kLevel29: return row;
    case kLevel39: return (row + 1) / 2;
  }
  unreachable();
}

constexpr int GetLastFrameOnRow(int row, Level level) {
  switch (level) {
    case kLevel18: return row * 3 + 2;
    case kLevel19: return row * 2 + 1;
    case kLevel29: return row;
    case kLevel39: return row / 2;
  }
  unreachable();
}

template <
    int tap1, int tap2, int tap3, int tap4, int tap5,
    int tap6, int tap7, int tap8, int tap9, int tap10>
class TapTable {
  int t[10];
 public:
  constexpr TapTable() : t{tap1, tap2, tap3, tap4, tap5, tap6, tap7, tap8, tap9, tap10} {
    static_assert(
        tap1 >= 0 && tap2 >= 2 && tap3 >= 2 && tap4 >= 2 && tap5 >= 2 &&
        tap6 >= 2 && tap7 >= 2 && tap8 >= 2 && tap9 >= 2 && tap10 >= 2,
        "each tap must be at least 2 frames apart");
    for (int i = 1; i < 10; i++) t[i] += t[i - 1];
  }
  constexpr int operator[](int x) const { return t[x]; }
  constexpr const int* data() const { return t; }
};

template <class T> struct IsTapTable : std::false_type {};
template <int... args> struct IsTapTable<TapTable<args...>> : std::true_type {};

constexpr int abs(int x) { return x < 0 ? -x : x; }
constexpr int sgn(int x) {
  return x == 0 ? 0 : x > 0 ? 1 : -1;
}

// Check each bit in mask is set in board
template <int R>
constexpr bool Contains(const std::array<Board, R>& board, const std::array<Board, R>& mask) {
  bool ret = true;
  for (int i = 0; i < R; i++) ret &= (board[i] & mask[i]) == mask[i];
  return ret;
}

template <int R>
struct TableEntry {
  // Each entry corresponds to a (rot, col) that is possible to reach
  // `num_taps` is the minimum number of taps needed to reach that (rot, col)
  //   (so taps[num_taps-1] will be the exact frame when the piece is moved to (rot, col))
  // This (rot, col) is only reachable if the cells corresponding to `masks` in entry `prev`
  //   (prev is regarded recursively; that is, the `prev` of `prev`, etc., should also be included)
  //   and `masks_nodrop` in this entry are all empty
  //   (`masks_nodrop` should be a subset of `masks` in the same entry)
  uint8_t rot, col, prev, num_taps;
  // `cannot_finish` means no further input is possible since it will be locked at the bottom of the board
  // when it happens, `masks` should not be used
  bool cannot_finish;
  std::array<Board, R> masks, masks_nodrop;

  bool operator<=>(const TableEntry<R>&) const = default;
};

template <Level level, int R, class Taps>
constexpr int Phase1TableGen(int initial_frame, int initial_rot, int initial_col, TableEntry<R> entries[]) {
#ifndef _MSC_VER
  static_assert(IsTapTable<Taps>::value);
#endif
  int sz = 0;
  static_assert(R == 1 || R == 2 || R == 4, "unexpected rotations");
  constexpr uint8_t kA = 0x1;
  constexpr uint8_t kB = 0x2;
  constexpr uint8_t kL = 0x4;
  constexpr uint8_t kR = 0x8;
  constexpr Taps taps{};
  std::array<Board, R> masks[R][10] = {};
  std::array<Board, R> masks_nodrop[R][10] = {};
  uint8_t last_tap[R][10] = {};
  bool cannot_reach[R][10] = {};
  bool cannot_finish[R][10] = {};
  for (int i = 0; i < R; i++) {
    for (int j = 0; j < 10; j++) {
      for (int k = 0; k < R; k++) masks[i][j][k] = Board(0, 0, 0, 0);
    }
  }
  for (int col = 0; col < 10; col++) {
    for (int delta_rot = 0; delta_rot < 4; delta_rot++) {
      // piece end up at column col and rotation (initial_rot + delta_rot)
      if (R == 1 && delta_rot != 0) continue;
      if (R == 2 && delta_rot >= 2) continue;
      int rot = (initial_rot + delta_rot) % R;
      int num_lr_tap = abs(col - initial_col);
      int num_ab_tap = delta_rot == 3 ? 1 : delta_rot; // [0,1,2,1]
      int num_tap = std::max(num_ab_tap, num_lr_tap);
      // the frame that this tap occurred; initial_frame if no input
      int start_frame = (num_tap == 0 ? 0 : taps[num_tap - 1]) + initial_frame;
      // the frame that next input is allowed
      int end_frame = taps[num_tap] + initial_frame;
      if (num_tap) {
        if (num_tap == num_lr_tap) last_tap[rot][col] |= col > initial_col ? kR : kL;
        if (num_tap == num_ab_tap) last_tap[rot][col] |= delta_rot == 3 ? kB : kA;
      }
      // the position before this tap
      int start_row = GetRow(start_frame, level);
      if (start_row >= 20) {
        cannot_reach[rot][col] = true;
        continue;
      }
      int start_col = num_tap == num_lr_tap ? col - sgn(col - initial_col) : col;
      int start_rot = num_tap == num_ab_tap ? ((delta_rot == 2 ? 1 : 0) + initial_rot) % R : rot;
      auto& cur = masks[rot][col];
      cur[start_rot].Set(start_row, start_col);
      cur[start_rot].Set(start_row, col); // first shift
      cur[rot].Set(start_row, col); // then rotate
      masks_nodrop[rot][col] = cur;
      if (GetRow(end_frame, level) >= 20) {
        cannot_finish[rot][col] = true;
        continue;
      }
      for (int frame = start_frame; frame < end_frame; frame++) {
        int row = GetRow(frame, level);
        cur[rot].Set(row, col);
        if (IsDropFrame(frame, level)) {
          cur[rot].Set(row + 1, col);
          if (level == kLevel39) cur[rot].Set(row + 2, col);
        }
      }
    }
  }
  // start from (initial_col, initial_row) and build the entries according to
  //   ascending tap count
  auto Push = [&](uint8_t rot, uint8_t col, uint8_t prev, uint8_t num_taps) {
    if (!cannot_reach[rot][col]) {
      entries[sz] = {rot, col, prev, num_taps, cannot_finish[rot][col], {}, {}};
      for (int i = 0; i < R; i++) {
        entries[sz].masks[i] = masks[rot][col][i];
        entries[sz].masks_nodrop[i] = masks_nodrop[rot][col][i];
      }
      sz++;
    }
  };
  Push(initial_rot, initial_col, 0, 0);
  for (int cur = 0; cur < sz; cur++) {
    int rot = entries[cur].rot, col = entries[cur].col, taps = entries[cur].num_taps;
    int last = last_tap[rot][col];
    bool should_l = col > 0 && (taps == 0 || (last & kL));
    bool should_r = col < 9 && (taps == 0 || (last & kR));
    bool should_a = (R > 1 && taps == 0) || (R == 4 && taps == 1 && (last & kA));
    bool should_b = R == 4 && taps == 0;
    if (should_l) Push(rot, col - 1, cur, taps + 1); // L
    if (should_r) Push(rot, col + 1, cur, taps + 1); // R
    if (should_a) {
      int nrot = (rot + 1) % R;
      Push(nrot, col, cur, taps + 1); // A
      if (should_l) Push(nrot, col - 1, cur, taps + 1); // L
      if (should_r) Push(nrot, col + 1, cur, taps + 1); // R
    }
    if (should_b) {
      int nrot = (rot + 3) % R;
      Push(nrot, col, cur, taps + 1); // A
      if (should_l) Push(nrot, col - 1, cur, taps + 1); // L
      if (should_r) Push(nrot, col + 1, cur, taps + 1); // R
    }
  }
  return sz;
}

template <Level level, int R, int adj_frame, class Taps>
struct Phase1Table {
  static constexpr int initial_N = Phase1TableGen<level, R, Taps>(
      0, 0, Position::Start.y, std::array<TableEntry<R>, 10*R>().data());
  TableEntry<R> initial[initial_N];
  int adj_N[initial_N];
  TableEntry<R> adj[initial_N][10 * R];
  constexpr Phase1Table() : initial{}, adj_N{}, adj{} {
    constexpr Taps taps{};
    Phase1TableGen<level, R, Taps>(0, 0, Position::Start.y, initial);
    for (int i = 0; i < initial_N; i++) {
      int frame_start = std::max(adj_frame, taps[initial[i].num_taps]);
      adj_N[i] = Phase1TableGen<level, R, Taps>(
          frame_start, initial[i].rot, initial[i].col, adj[i]);
    }
  }
};

// Column is simply a column; each bit is a cell, LSB is topmost
// Frames is a processed form of a column; each bit is a frame, LSB is the first frame
// Frames comes in two flavors: normal and drop mask
//   normal mask just corresponds to the row the piece is in on each frame
//   drop mask is AND of all rows that the piece will pass when dropping
// For level 18,19,29, drop mask is just (normal_mask & normal_mask >> 1);
//   drop mask exists only to make it easy to deal with level 39
using Column = uint32_t;
using Frames = uint64_t;

template <int R>
struct FrameMasks {
  Frames frame[R][10], drop[R][10];
};

template <Level level>
constexpr Frames ColumnToNormalFrameMask(Column col) {
  switch (level) {
    case kLevel18: {
      constexpr uint64_t kMask = 0x249249249249249;
      uint64_t expanded = pdep<uint64_t>(col, kMask);
      return expanded | expanded << 1 | expanded << 2;
    }
    case kLevel19: {
      constexpr uint64_t kMask = 0x5555555555;
      uint64_t expanded = pdep<uint64_t>(col, kMask);
      return expanded | expanded << 1;
    }
    case kLevel29: return col;
    case kLevel39: {
      constexpr uint32_t kMask = 0x55555;
      return pext(col, kMask);
    }
  }
  unreachable();
}

template <Level level>
constexpr Frames ColumnToDropFrameMask(Column col) {
  switch (level) {
    case kLevel18: [[fallthrough]];
    case kLevel19: [[fallthrough]];
    case kLevel29: {
      uint64_t mask = ColumnToNormalFrameMask<level>(col);
      return mask & mask >> 1;
    }
    case kLevel39: {
      constexpr uint32_t kMask = 0x55555;
      return pext(col & col >> 1 & col >> 2, kMask);
    }
  }
  unreachable();
}

template <Level level>
constexpr Column FramesToColumn(Frames frames) {
  switch (level) {
    case kLevel18: {
      constexpr uint64_t kMask = 0x249249249249249;
      return pext(frames | frames >> 1 | frames >> 2, kMask);
    }
    case kLevel19: {
      constexpr uint64_t kMask = 0x5555555555;
      return pext(frames | frames >> 1, kMask);
    }
    case kLevel29: return frames;
    case kLevel39: {
      constexpr uint32_t kMask = 0x55555;
      return pdep<uint32_t>(frames, kMask);
    }
  }
}

constexpr int FindLockRow(uint32_t col, int start_row) {
  // given (col & 1<<row) != 0
  // col               = 00111100011101
  // 1<<row            = 00000000001000
  // col+(1<<row)      = 00111100100101
  // col^(col+(1<<row))= 00000000111000
  //              highbit=31-clz ^
  return 31 - clz<uint32_t>(col ^ (col + (1 << start_row))) - 1;
}

// note: "tuck" here means tucks, spins or spintucks
template <int R>
using TuckMask = std::array<std::array<Frames, 10>, R>;

constexpr int TuckTypes(int R) {
  return (R == 1 ? 2 : R == 2 ? 7 : 12) + (kDoubleTuckAllowed ? 2 : 0);
  // R = 1: L R (LL RR)
  // R = 2: A LA RA AL AR
  // R = 4: B LB RB BL BR
  // should also change frame_sequence.h if changed
  // it is possible to add other tuck types suck as buco-like spins
  // but we just keep it simple here
}

template <int R>
using TuckMasks = std::array<TuckMask<R>, TuckTypes(R)>;

struct TuckType {
  int delta_rot, delta_col, delta_frame;
};

template <int R>
struct TuckTypeTable {
  std::array<TuckType, TuckTypes(R)> table;
  constexpr TuckTypeTable() : table() {
    table[0] = {0, -1, 0}; // L
    table[1] = {0, 1, 0}; // R
#ifdef DOUBLE_TUCK
    table[2] = {0, -2, 2}; // L-/-L
    table[3] = {0, 2, 2}; // R-/-R
#endif
    constexpr int x = kDoubleTuckAllowed ? 2 : 0;
    if constexpr (R == 1) return;
    table[x+2] = {1, 0, 0}; // A
    table[x+3] = {1, -1, 0}; // LA
    table[x+4] = {1, 1, 0}; // RA
    table[x+5] = {1, -1, 1}; // A-L L-A
    table[x+6] = {1, 1, 1}; // A-R R-A
    if constexpr (R == 2) return;
    table[x+7] = {3, 0, 0}; // B
    table[x+8] = {3, -1, 0}; // LB
    table[x+9] = {3, 1, 0}; // RB
    table[x+10] = {3, -1, 1}; // B-L L-B
    table[x+11] = {3, 1, 1}; // B-R R-B
  }
};

template <int R>
constexpr TuckMasks<R> GetTuckMasks(const FrameMasks<R> m) {
  TuckMasks<R> ret{};
  constexpr int x = kDoubleTuckAllowed ? 2 : 0;
#pragma GCC unroll 4
  for (int rot = 0; rot < R; rot++) {
    for (int col = 0; col < 10; col++) {
      if (col > 0) ret[0][rot][col] = m.frame[rot][col] & m.frame[rot][col-1];
      if (col < 9) ret[1][rot][col] = m.frame[rot][col] & m.frame[rot][col+1];
#ifdef DOUBLE_TUCK
      if (col > 1) ret[2][rot][col] = m.frame[rot][col] & m.drop[rot][col-1] & m.drop[rot][col-1] >> 1 & m.frame[rot][col-2] >> 2;
      if (col < 8) ret[3][rot][col] = m.frame[rot][col] & m.drop[rot][col+1] & m.drop[rot][col+1] >> 1 & m.frame[rot][col+2] >> 2;
#endif
    }
  }
  if (R == 1) return ret;
#pragma GCC unroll 4
  for (int rot = 0; rot < R; rot++) {
    int nrot = (rot + 1) % R;
    for (int col = 0; col < 10; col++) {
      ret[x+2][rot][col] = m.frame[rot][col] & m.frame[nrot][col];
      if (col > 0) ret[x+3][rot][col] = ret[0][rot][col] & m.frame[nrot][col-1];
      if (col < 9) ret[x+4][rot][col] = ret[1][rot][col] & m.frame[nrot][col+1];
      if (col > 0) ret[x+5][rot][col] = m.frame[rot][col] & (m.drop[nrot][col] | m.drop[rot][col-1]) & m.frame[nrot][col-1] >> 1;
      if (col < 9) ret[x+6][rot][col] = m.frame[rot][col] & (m.drop[nrot][col] | m.drop[rot][col+1]) & m.frame[nrot][col+1] >> 1;
    }
  }
  if (R == 2) return ret;
#pragma GCC unroll 4
  for (int rot = 0; rot < R; rot++) {
    int nrot = (rot + 3) % R;
    for (int col = 0; col < 10; col++) {
      ret[x+7][rot][col] = m.frame[rot][col] & m.frame[nrot][col];
      if (col > 0) ret[x+8][rot][col] = ret[0][rot][col] & m.frame[nrot][col-1];
      if (col < 9) ret[x+9][rot][col] = ret[1][rot][col] & m.frame[nrot][col+1];
      if (col > 0) ret[x+10][rot][col] = m.frame[rot][col] & (m.drop[nrot][col] | m.drop[rot][col-1]) & m.frame[nrot][col-1] >> 1;
      if (col < 9) ret[x+11][rot][col] = m.frame[rot][col] & (m.drop[nrot][col] | m.drop[rot][col+1]) & m.frame[nrot][col+1] >> 1;
    }
  }
  return ret;
}

template <Level level, int R, int adj_frame, class Taps>
class Search {
  static constexpr Taps taps{};
  static constexpr Phase1Table<level, R, adj_frame, Taps> table{};

  NOINLINE constexpr void RunPhase2(
      const Column cols[R][10],
      const TuckMasks<R> tuck_masks,
      const Column lock_positions_without_tuck[R][10],
      const Frames can_tuck_frame_masks[R][10],
      int& sz, Position* positions) const {
    auto Insert = [&](const Position& pos) {
      positions[sz++] = pos;
    };
    constexpr TuckTypeTable<R> tucks;
    Frames tuck_result[R][10] = {};
    for (int i = 0; i < TuckTypes(R); i++) {
      const auto& tuck = tucks.table[i];
      int start_col = std::max(0, -tuck.delta_col);
      int end_col = std::min(10, 10 - tuck.delta_col);
      for (int rot = 0; rot < R; rot++) {
        int nrot = (rot + tuck.delta_rot) % R;
        for (int col = start_col; col < end_col; col++) {
          tuck_result[nrot][col + tuck.delta_col] |=
              (tuck_masks[i][rot][col] & can_tuck_frame_masks[rot][col]) << tuck.delta_frame;
        }
      }
    }
    for (int rot = 0; rot < R; rot++) {
      for (int col = 0; col < 10; col++) {
        Column after_tuck_positions = FramesToColumn<level>(tuck_result[rot][col]);
        Column cur = cols[rot][col];
        Column tuck_lock_positions = (after_tuck_positions + cur) >> 1 & (cur & ~cur >> 1) & ~lock_positions_without_tuck[rot][col];
        while (tuck_lock_positions) {
          int row = ctz(tuck_lock_positions);
          Insert({rot, row, col});
          tuck_lock_positions ^= 1 << row;
        }
      }
    }
  }

  // N = is_adj ? table.adj_N[X] : initial_N
  // table = is_adj ? table.adj[X] : table.initial
  // initial_frame = is_adj ? <the last> : 0
  template <bool is_adj, int initial_id = 0> constexpr int DoOneSearch(
      const std::array<Board, R>& board, const Column cols[R][10],
      const TuckMasks<R> tuck_masks,
      bool can_adj[],
      Position* positions) const {
    constexpr int total_frames = GetLastFrameOnRow(19, level) + 1;
    constexpr int N = is_adj ? table.adj_N[initial_id] : table.initial_N;
    constexpr int initial_frame = is_adj ? std::max(adj_frame, taps[table.initial[initial_id].num_taps]) : 0;
    if (initial_frame >= total_frames) return 0;
    if (is_adj && !can_adj[initial_id]) return 0;

    int sz = 0;
    auto Insert = [&](const Position& pos) {
      positions[sz++] = pos;
    };
    // phase 1
    bool can_continue[R * 10] = {}; // whether the next tap can continue
    Frames can_tuck_frame_masks[R][10] = {}; // frames that can start a tuck
    Column lock_positions_without_tuck[R][10] = {};

    bool phase_2_possible = false;
    bool can_reach[std::max(N, 1)] = {};
    For<N>([&](auto i_obj) {
      constexpr int i = i_obj.value;
      constexpr auto& entry = is_adj ? table.adj[initial_id][i] : table.initial[i];
      if (i && !can_continue[entry.prev]) return;
      if (!entry.cannot_finish && Contains<R>(board, entry.masks)) {
        can_continue[i] = true;
      } else if (!Contains<R>(board, entry.masks_nodrop)) {
        return;
      }
      can_reach[i] = true;
    });
#pragma GCC unroll 0
    for (int i = 0; i < N; i++) {
      if (!can_reach[i]) continue;
      auto& entry = is_adj ? table.adj[initial_id][i] : table.initial[i];
      int start_frame = (entry.num_taps == 0 ? 0 : taps[entry.num_taps - 1]) + initial_frame;
      int start_row = GetRow(start_frame, level);
      int end_frame = is_adj ? total_frames : std::max(adj_frame, taps[entry.num_taps]);
      // Since we verified masks_nodrop, start_row should be in col
      //if ((cols[entry.rot][entry.col] & 1 << start_row) == 0) throw std::runtime_error("unexpected");
      int lock_row = FindLockRow(cols[entry.rot][entry.col], start_row);
      int lock_frame = GetLastFrameOnRow(lock_row, level) + 1;
      if (!is_adj && lock_frame > end_frame) {
        can_adj[i] = true;
      } else {
        Insert({entry.rot, lock_row, entry.col});
      }
      int first_tuck_frame = initial_frame + taps[entry.num_taps];
      int last_tuck_frame = std::min(lock_frame, end_frame);
      lock_positions_without_tuck[entry.rot][entry.col] |= 1 << lock_row;
      if (last_tuck_frame > first_tuck_frame) {
        can_tuck_frame_masks[entry.rot][entry.col] = (1ll << last_tuck_frame) - (1ll << first_tuck_frame);
        phase_2_possible = true;
      }
    }
    if (phase_2_possible) {
      RunPhase2(cols, tuck_masks, lock_positions_without_tuck, can_tuck_frame_masks, sz, positions);
    }
    return sz;
  }

 public:
  /** drop sequence:
  *
  * initial phase 1         adj phase 1
  * vvvvvvvvvvv               vvvvvvv
  * L - L - L - - - - - - - - R - R - - - - - - -<lock>
  *               \           ^ adj_frame   \
  *                \A R - - -<lock>          \B R - - -<lock>
  *                 ^^^^                      ^^^^
  *                initial phase 2 (tuck)    adj phase 2
  */
  PossibleMoves MoveSearch(const std::array<Board, R>& board) const {
    constexpr int initial_N = decltype(table)::initial_N;
    Column cols[R][10] = {};
    FrameMasks<R> frame_masks = {};
    for (int rot = 0; rot < R; rot++) {
      for (int col = 0; col < 10; col++) {
        cols[rot][col] = board[rot].Column(col);
        // ColumnToNormalFrameMask<level>(col), ColumnToDropFrameMask<level>(col)
        frame_masks.frame[rot][col] = ColumnToNormalFrameMask<level>(cols[rot][col]);
        frame_masks.drop[rot][col] = ColumnToDropFrameMask<level>(cols[rot][col]);
      }
    }
    auto tuck_masks = GetTuckMasks<R>(frame_masks);
    bool can_adj[initial_N] = {}; // whether adjustment starting from this (rot, col) is possible

    PossibleMoves ret;
    Position buf[256];
    ret.non_adj.assign(buf, buf + DoOneSearch<false>(
        board, cols, tuck_masks, can_adj, buf));

    For<initial_N>([&](auto i_obj) {
      constexpr int initial_id = i_obj.value;
      auto& entry = table.initial[initial_id];
      int x = DoOneSearch<true, initial_id>(board, cols, tuck_masks, can_adj, buf);
      if (x) {
        int row = GetRow(std::max(adj_frame, taps[entry.num_taps]), level);
        ret.adj.emplace_back(Position{entry.rot, row, entry.col}, std::vector<Position>(buf, buf + x));
      }
    });
    return ret;
  }
};

} // namespace move_search

template <Level level, int R, int adj_frame, class Taps>
NOINLINE PossibleMoves MoveSearch(const std::array<Board, R>& board) {
  constexpr move_search::Search<level, R, adj_frame, Taps> search{};
  return search.MoveSearch(board);
}

template <Level level, int adj_frame, class Taps>
PossibleMoves MoveSearch(const Board& b, int piece) {
#define PIECE_CASE_TMPL_ARGS ,adj_frame,Taps
#define PIECE_CASE_ARGS
  DO_PIECE_CASE(MoveSearch, b);
#undef PIECE_CASE_TMPL_ARGS
#undef PIECE_CASE_ARGS
}

template <int adj_frame, class Taps>
PossibleMoves MoveSearch(const Board& b, Level level, int piece) {
#define LEVEL_CASE_TMPL_ARGS ,adj_frame,Taps
  DO_LEVEL_CASE(MoveSearch, b, piece);
#undef LEVEL_CASE_TMPL_ARGS
}

using Tap30Hz = move_search::TapTable<0, 2, 2, 2, 2, 2, 2, 2, 2, 2>;
using Tap20Hz = move_search::TapTable<0, 3, 3, 3, 3, 3, 3, 3, 3, 3>;
using Tap15Hz = move_search::TapTable<0, 4, 4, 4, 4, 4, 4, 4, 4, 4>;
using Tap12Hz = move_search::TapTable<0, 5, 5, 5, 5, 5, 5, 5, 5, 5>;
