#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"
#include <algorithm>
#include <cmath>
#include <vector>
#define PIXEL_SIZE 6

class RoadEditor : public olc::PixelGameEngine
{
public:
	RoadEditor()
	{
		sAppName = "Road editor";
	}

private:
  class Node {
  public:
    unsigned int id;
    olc::vf2d vPos; // Relative to the WORLD CENTER
    olc::Pixel pColor;
    float fRadius;
    bool bSelected = false;
    Node(olc::vf2d pos=olc::vf2d(0, 0), float radius=3.0f, olc::Pixel color=olc::BLUE): vPos(pos), pColor(color), fRadius(radius) {
    }
  };

  struct Connection {
    unsigned int a; // First node
    unsigned int b; // Second node
    bool bSelected = false;
    int nLines = 1;
  };

	olc::vi2d vLastWindowSize = { 0, 0 };
  olc::vf2d vScreenHalf;

  olc::vi2d vPos;

  olc::vi2d vMouse;
  olc::vi2d vMouseOld;

  bool bIsPanning = false;

  olc::vf2d vCursorPos;

  bool bSelectionMoved = false;
  bool bSelecting = false;
  olc::vf2d vSelectionStartWorld;
  olc::Pixel pSelectionColor = olc::PixelF(0.0f, 0.5f, 0.8f, 0.35f);


  bool bShowGrid = true;
  bool bShowNodes = true;
  int gridSpacingUnits = 10;
  olc::Pixel pGridColor = olc::PixelF(0, 0, 0, 0.05);


  std::vector<Node> nodes;
  unsigned int nNextNodeId = 0;
  float fNodeRadius = 3.0f;

  std::vector<Connection> connections;
  // While SHIFT is held, each new node is linked to the previous one created during the same hold. -1 means no previous node yet
  int nLastNodeId = -1;

  int nSelectedNodeId = -1; // node picked with RMB, source of a manual connection

  // A new node may only be placed if it keeps at least 2px of clearance from every existing node, accounting for both nodes' radiuses
  bool CanPlaceNode(const olc::vf2d& vPos, float fRadius) const {
    for (const auto& node : nodes) {
      float fMinDist = node.fRadius + fRadius + 2.0f;
      if ((node.vPos - vPos).mag2() < fMinDist * fMinDist) return false;
    }
    return true;
  }

  const Node* FindNode(unsigned int id) const {
    for (const auto& node : nodes) if (node.id == id) return &node;
    return nullptr;
  }

  // Return the id of the node under the given world position, or -1 if none
  int NodeAtWorld(const olc::vf2d& vWorld) const {
    for (const auto& node : nodes) {
      float fPick = node.fRadius + 2.0f; // small tolerance for easier clicking
      if ((node.vPos - vWorld).mag2() <= fPick * fPick) return (int)node.id;
    }
    return -1;
  }

  // Clear any LMB-drag selection on both nodes and connections
  void ClearSelection() {
    for (auto& node : nodes) node.bSelected = false;
    for (auto& c : connections) c.bSelected = false;
  }

  bool AnythingSelected() const {
    for (const auto& node : nodes) if (node.bSelected) return true;
    for (const auto& c : connections) if (c.bSelected) return true;
    return false;
  }

  // Does the segment p0->p1 intersect (or lie within) the axis-aligned box?
  static bool SegmentIntersectsRect(const olc::vf2d& p0, const olc::vf2d& p1, const olc::vf2d& tl, const olc::vf2d& br) {
    // Either endpoint inside the box counts as a hit
    auto inside = [&](const olc::vf2d& p) {
      return p.x >= tl.x && p.x <= br.x && p.y >= tl.y && p.y <= br.y;
    };
    if (inside(p0) || inside(p1)) return true;

    // Otherwise test the segment against each of the four box edges
    auto ccw = [](const olc::vf2d& a, const olc::vf2d& b, const olc::vf2d& c) {
      return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };
    auto segSeg = [&](const olc::vf2d& a, const olc::vf2d& b,
                      const olc::vf2d& c, const olc::vf2d& d) {
      float d1 = ccw(a, b, c), d2 = ccw(a, b, d);
      float d3 = ccw(c, d, a), d4 = ccw(c, d, b);
      return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
    };
    olc::vf2d tr(br.x, tl.y), bl(tl.x, br.y);
    return segSeg(p0, p1, tl, tr) || segSeg(p0, p1, tr, br) ||
           segSeg(p0, p1, br, bl) || segSeg(p0, p1, bl, tl);
  }

  // Mark every node inside the world-space selection box, and every connection
  // whose line crosses it, as selected (clearing any prior selection first)
  void UpdateDragSelection(const olc::vf2d& vA, const olc::vf2d& vB) {
    olc::vf2d tl(std::min(vA.x, vB.x), std::min(vA.y, vB.y));
    olc::vf2d br(std::max(vA.x, vB.x), std::max(vA.y, vB.y));
    for (auto& node : nodes) {
      node.bSelected = node.vPos.x >= tl.x && node.vPos.x <= br.x &&
                       node.vPos.y >= tl.y && node.vPos.y <= br.y;
    }
    for (auto& c : connections) {
      const Node* na = FindNode(c.a);
      const Node* nb = FindNode(c.b);
      c.bSelected = na && nb && SegmentIntersectsRect(na->vPos, nb->vPos, tl, br);
    }
  }

  void DeleteSelection() {
    std::vector<unsigned int> doomed;
    for (const auto& node : nodes) {
      if (node.bSelected) {
        doomed.push_back(node.id);
      }
    }
    auto touchesDoomed = [&](unsigned int id) {
      return std::find(doomed.begin(), doomed.end(), id) != doomed.end();
    };

    connections.erase(std::remove_if(connections.begin(), connections.end(), [&](const Connection& c) {
      return c.bSelected || touchesDoomed(c.a) || touchesDoomed(c.b);
    }), connections.end());

    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [](const Node& n) { return n.bSelected; }), nodes.end());

    // A deleted node must not linger as a connection source
    if (nSelectedNodeId >= 0 && touchesDoomed((unsigned int)nSelectedNodeId)) {
      nSelectedNodeId = -1;
    }
  }

  static constexpr float fLineSpacing = 2.0f;

  void DrawConnections(olc::Pixel col, olc::Pixel selCol) {
    for (const auto& c : connections) {
      const Node* na = FindNode(c.a);
      const Node* nb = FindNode(c.b);
      if (!na || !nb) continue;
      olc::vf2d sa = WorldToScreen(na->vPos);
      olc::vf2d sb = WorldToScreen(nb->vPos);
      olc::Pixel p = c.bSelected ? selCol : col;
      if (c.nLines <= 1) {
        DrawLineDecal(sa, sb, p);
        continue;
      }
      olc::vf2d d = sb - sa;
      float fMag = d.mag();
      olc::vf2d perp = fMag > 0 ? olc::vf2d(-d.y, d.x) / fMag : olc::vf2d(0, 0);
      for (int i = 0; i < c.nLines; i++) {
        olc::vf2d o = perp * ((i - (c.nLines - 1) * 0.5f) * fLineSpacing);
        DrawLineDecal(sa + o, sb + o, p);
      }
    }
  }

  void AdjustSelectedLines(int delta) {
    for (auto& c : connections) {
      if (c.bSelected) c.nLines = std::clamp(c.nLines + delta, 1, 3);
    }
  }

  olc::vf2d WorldToScreen(const olc::vf2d& vWorld) const {
    return vScreenHalf + vPos - vWorld;
  }
  olc::vf2d ScreenToWorld(const olc::vf2d& vScreen) const {
    return vScreenHalf + vPos - vScreen;
  }

public:
	bool OnUserCreate() override
	{
		vLastWindowSize = GetWindowSize();
    vScreenHalf = GetScreenSize()/2;

		return true;
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
    if (GetKey(olc::Key::ESCAPE).bPressed || GetKey(olc::Key::CTRL).bHeld && GetKey(olc::Key::Q).bPressed) {
      return 0;
    }
    // RESIZE LOGIC
		olc::vi2d vCurrentWindowSize = GetWindowSize();
		if (vCurrentWindowSize != vLastWindowSize && vCurrentWindowSize.x > 0 && vCurrentWindowSize.y > 0)
		{
			SetScreenSize(int(vCurrentWindowSize.x/PIXEL_SIZE), int(vCurrentWindowSize.y/PIXEL_SIZE));
			vLastWindowSize = vCurrentWindowSize;
      vScreenHalf = GetScreenSize() /2;
		}
    vMouse = GetMousePos();
    if (GetKey(olc::Key::CTRL).bHeld) { // snap to nearest grid intersection
      olc::vf2d vWorldOrigin = WorldToScreen(olc::vf2d(0, 0)); // origin in screen space
      olc::vf2d vRel = olc::vf2d(vMouse) - vWorldOrigin;
      vRel.x = std::round(vRel.x / gridSpacingUnits) * gridSpacingUnits;
      vRel.y = std::round(vRel.y / gridSpacingUnits) * gridSpacingUnits;
      vMouse = vWorldOrigin + vRel;
    }
    vCursorPos = ScreenToWorld(vMouse); // World space cursor pos

    if (GetKey(olc::Key::SHIFT).bPressed || GetKey(olc::Key::SHIFT).bReleased) {
      nLastNodeId = -1;
    }

    if (GetMouse(0).bPressed) { // LMB: anchor the selection start in WORLD space
      vSelectionStartWorld = vCursorPos;
      bSelectionMoved = false;
      ClearSelection(); // a fresh drag replaces any previous selection
    }
    if (GetMouse(0).bHeld) {
      if (vMouse != vMouseOld) {
        bSelectionMoved = true; // only a real drag selects
      }
      bSelecting = bSelectionMoved;
      if (bSelecting) {
        UpdateDragSelection(vSelectionStartWorld, vCursorPos);
      }
    } else {
      if (GetMouse(0).bReleased && !bSelectionMoved && GetKey(olc::Key::SHIFT).bHeld) {
        // SHIFT + click (no drag) creates a node at the cursor's world position,
        // but only where it stays clear of every existing node
        if (CanPlaceNode(vCursorPos, fNodeRadius)) {
          Node node(vCursorPos, fNodeRadius);
          node.id = nNextNodeId++;
          nodes.push_back(node);
          if (nLastNodeId >= 0) {
            connections.push_back({ (unsigned int)nLastNodeId, node.id });
          }
          nLastNodeId = (int)node.id;
        }
      }
      bSelectionMoved = false;
      bSelecting = false;
    }
    if (GetMouse(1).bPressed) { // RMB: select a node, or connect to the selected one
      int nHit = NodeAtWorld(vCursorPos);
      if (nHit >= 0) {
        if (GetKey(olc::Key::SHIFT).bHeld && nSelectedNodeId >= 0 && nSelectedNodeId != nHit) {
          connections.push_back({ (unsigned int)nSelectedNodeId, (unsigned int)nHit });
          nSelectedNodeId = -1;
        } else {
          nSelectedNodeId = nHit;
        }
      }
    }
    if (GetMouse(2).bHeld) { // MMB
      bIsPanning = true;
    } else {
      bIsPanning = false;
    }
    if (bIsPanning) {
      vPos += vMouse - vMouseOld;
    }

    if (GetKey(olc::Key::G).bPressed) {
      bShowGrid = !bShowGrid;
    }

    if (GetKey(olc::Key::H).bPressed) {
      bShowNodes = !bShowNodes;
    }

    if ((GetKey(olc::Key::DEL).bPressed || GetKey(olc::Key::BACK).bPressed) && AnythingSelected()) {
      DeleteSelection();
    }

    if (GetKey(olc::Key::EQUALS).bPressed || GetKey(olc::Key::NP_ADD).bPressed) {
      AdjustSelectedLines(1);
    }
    if (GetKey(olc::Key::MINUS).bPressed || GetKey(olc::Key::NP_SUB).bPressed) {
      AdjustSelectedLines(-1);
    }

    vMouseOld = vMouse;


		Clear(olc::WHITE);

    if (bShowGrid) {
      olc::vf2d vWorldOrigin = WorldToScreen(olc::vf2d(0, 0)); // origin in screen space

      float startX = std::fmod(vWorldOrigin.x, (float)gridSpacingUnits);
      if (startX < 0) {
        startX += gridSpacingUnits;
      }
      float startY = std::fmod(vWorldOrigin.y, (float)gridSpacingUnits);
      if (startY < 0) {
        startY += gridSpacingUnits;
      }

      for (float x = startX; x < ScreenWidth(); x += gridSpacingUnits) { // vertical stripes
        FillRectDecal(olc::vf2d(x, 0), olc::vf2d(1, ScreenHeight()), pGridColor);
      }
      for (float y = startY; y < ScreenHeight(); y += gridSpacingUnits) { // horizontal stripes
        FillRectDecal(olc::vf2d(0, y), olc::vf2d(ScreenWidth(), 1), pGridColor);
      }
    }

    // DrawCircle(WorldToScreen(olc::vf2d(0, 0)), 10, olc::BLACK); // In the world center

    DrawConnections(olc::DARK_GREY, olc::RED);

    if (bShowNodes) {
      for (const auto& node : nodes) {
        FillCircle(WorldToScreen(node.vPos), (int)node.fRadius, node.pColor);
        if (node.bSelected) {
          DrawCircle(WorldToScreen(node.vPos), (int)node.fRadius + 2, olc::RED);
        }
      }

      if (nSelectedNodeId >= 0) {
        const Node* sel = FindNode((unsigned int)nSelectedNodeId);
        if (sel) DrawCircle(WorldToScreen(sel->vPos), (int)sel->fRadius + 2, olc::RED);
      }
    }

    if (bSelecting) {
      olc::vf2d vStart = WorldToScreen(vSelectionStartWorld);
      olc::vf2d vEnd = vMouse;
      olc::vf2d vTopLeft = { std::min(vStart.x, vEnd.x), std::min(vStart.y, vEnd.y) };
      olc::vf2d vBotRight = { std::max(vStart.x, vEnd.x), std::max(vStart.y, vEnd.y) };
      olc::vf2d vSize = vBotRight - vTopLeft + olc::vf2d(1, 1);
      FillRectDecal(vTopLeft, vSize, pSelectionColor);
    }

    // UI
    // Crosshair cursor
    float fLen = 5.0f;
    float fWid = 1.0f;
    olc::vf2d vM = vMouse;
    FillRectDecal(vM - olc::vf2d(fLen / 2.0f, 0), olc::vf2d(fLen+1, fWid), olc::RED); // horizontal
    FillRectDecal(vM - olc::vf2d(0, fLen / 2.0f), olc::vf2d(fWid, fLen+1), olc::RED); // vertical

    // World and Cursor positions
    DrawStringDecal(olc::vf2d(2, 2), ("X="+std::to_string(vPos.x)+"\nY="+std::to_string(vPos.y)), olc::RED, olc::vf2d(0.5, 0.5));
    DrawStringDecal(olc::vf2d(2, 12), ("X="+std::to_string(vCursorPos.x)+"\nY="+std::to_string(vCursorPos.y)), olc::GREEN, olc::vf2d(0.5, 0.5));

		return true;
	}
};

int main()
{
  std::cout << "Welcome to the RoadEditor!\n";
  std::cout << "Controls:\nMouse Wheel drag: Panning\nCRTL: Snap cursor to grid\nSHIFT + LMB: Add a Node\nRMB: Select first node\nSHIFT + RMB: Connect first selected node to this node\nLMB drag: Select nodes/connections\nDELETE / BACKSPACE: Delete the selection\n+ / -: Adjust lines per selected connection (1-3)\nG: Toggle grid\nH: Toggle node visibility";

	RoadEditor app;

	// Construct(screen_w, screen_h, pixel_w, pixel_h, full_screen, vsync, cohesion, realwindow)
	if (app.Construct(256, 128, PIXEL_SIZE, PIXEL_SIZE, false, false, false, false))
		app.Start();

	return 0;
}
