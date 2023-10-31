#pragma once

#include <vector>
#include "../board.h"

std::vector<ByteBoard> GetPieceMap(const ByteBoard& field, int poly);
ByteBoard PlacePiece(const ByteBoard& b, int poly, int r, int x, int y);
int ClearLines(ByteBoard& field);