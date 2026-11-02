#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <alphalaneous.editortab_api/include/EditorTabs.hpp>
#include <cmath>
#include <optional>
#include <vector>

using namespace geode::prelude;

namespace sd {
	// true while our editor tab is open
	inline bool g_drawMode = false;

	inline int stickID()  { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("stick-object-id")); }
	inline int circleID() { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("circle-object-id")); }
	inline float stickThickness() { return static_cast<float>(Mod::get()->getSettingValue<double>("stick-thickness")); }
	inline float circleDiameter() { return static_cast<float>(Mod::get()->getSettingValue<double>("circle-diameter")); }
	inline float snapRadius()     { return static_cast<float>(Mod::get()->getSettingValue<double>("snap-radius")); }
	inline float dragThreshold()  { return static_cast<float>(Mod::get()->getSettingValue<double>("drag-threshold")); }

	inline float normDeg(float d) {
		d = std::fmod(d, 360.f);
		if (d < 0.f) d += 360.f;
		return d;
	}

	// take the ID of the first selected editor object and store it in settings
	inline void pickFromSelection(bool forStick) {
		auto ui = EditorUI::get();
		if (!ui) return;
		auto sel = ui->getSelectedObjects();
		if (!sel || sel->count() == 0) {
			Notification::create("Select an object in the editor first", NotificationIcon::Warning)->show();
			return;
		}
		auto obj = static_cast<GameObject*>(sel->objectAtIndex(0));
		int id = obj->m_objectID;
		Mod::get()->setSettingValue<int64_t>(forStick ? "stick-object-id" : "circle-object-id", id);
		Notification::create(
			fmt::format("{} object ID = {}", forStick ? "Stick" : "Circle", id),
			NotificationIcon::Success
		)->show();
	}
}

struct JointHit {
	CCPoint pos;
	bool existing;
};

class $modify(SDEditorUI, EditorUI) {
	struct Fields {
		Ref<CCDrawNode> preview;
		CCPoint start;
		CCPoint cur;
		bool startExisting = false;
		bool active = false;
		bool moved = false;
	};

	CCPoint sdLayerPos(CCTouch* t) {
		return m_editorLayer->m_objectLayer->convertToNodeSpace(t->getLocation());
	}

	// If there is already a circle object near p, snap to it (stateless: scans the level).
	JointHit sdFindJoint(CCPoint p) {
		float best = sd::snapRadius();
		int cid = sd::circleID();
		JointHit res{p, false};
		for (auto obj : CCArrayExt<GameObject*>(m_editorLayer->m_objects)) {
			if (!obj || obj->m_objectID != cid) continue;
			float d = ccpDistance(obj->getPosition(), p);
			if (d <= best) {
				best = d;
				res = {obj->getPosition(), true};
			}
		}
		return res;
	}

	static void sdScaleToBox(GameObject* o, float w, float h) {
		auto s = o->getContentSize();
		float bw = s.width  > 1.f ? s.width  : 30.f;
		float bh = s.height > 1.f ? s.height : 30.f;
		o->updateCustomScaleX(w / bw);
		o->updateCustomScaleY(h / bh);
	}

	void sdPlaceCircle(CCPoint p) {
		auto o = m_editorLayer->createObject(sd::circleID(), p, true);
		if (!o) return;
		float d = sd::circleDiameter();
		sdScaleToBox(o, d, d);
	}

	void sdPlaceStick(CCPoint a, CCPoint b) {
		auto mid = ccp((a.x + b.x) / 2.f, (a.y + b.y) / 2.f);
		float len = ccpDistance(a, b);
		float deg = std::atan2(b.y - a.y, b.x - a.x) * 180.f / static_cast<float>(M_PI);
		auto o = m_editorLayer->createObject(sd::stickID(), mid, true);
		if (!o) return;
		sdScaleToBox(o, len, sd::stickThickness());
		o->setRotation(sd::normDeg(-deg)); // GD rotates clockwise
	}

	void sdDrawPreview() {
		auto f = m_fields.self();
		if (!f->preview) {
			f->preview = CCDrawNode::create();
			m_editorLayer->m_objectLayer->addChild(f->preview, 10000);
		}
		f->preview->clear();
		ccColor4F col{0.2f, 1.f, 0.4f, 0.8f};
		float r = sd::circleDiameter() / 2.f;
		f->preview->drawDot(f->start, r, col);
		if (f->moved) {
			f->preview->drawSegment(f->start, f->cur, sd::stickThickness() / 2.f, col);
			f->preview->drawDot(f->cur, r, col);
		}
	}

	void sdClearPreview() {
		auto f = m_fields.self();
		if (f->preview) f->preview->clear();
	}

	bool ccTouchBegan(CCTouch* t, CCEvent* e) {
		if (!sd::g_drawMode) return EditorUI::ccTouchBegan(t, e);
		auto f = m_fields.self();
		auto hit = sdFindJoint(sdLayerPos(t));
		f->start = hit.pos;
		f->startExisting = hit.existing;
		f->cur = hit.pos;
		f->active = true;
		f->moved = false;
		sdDrawPreview();
		return true; // swallow: don't select / move objects while drawing
	}

	void ccTouchMoved(CCTouch* t, CCEvent* e) {
		auto f = m_fields.self();
		if (!sd::g_drawMode || !f->active) return EditorUI::ccTouchMoved(t, e);
		auto p = sdLayerPos(t);
		float zoom = m_editorLayer->m_objectLayer->getScale();
		if (!f->moved && ccpDistance(p, f->start) * zoom >= sd::dragThreshold()) f->moved = true;
		if (f->moved) {
			auto hit = sdFindJoint(p);
			f->cur = hit.pos;
		}
		sdDrawPreview();
	}

	void ccTouchEnded(CCTouch* t, CCEvent* e) {
		auto f = m_fields.self();
		if (!sd::g_drawMode || !f->active) return EditorUI::ccTouchEnded(t, e);
		f->active = false;
		sdClearPreview();

		if (!f->moved) {
			if (!f->startExisting) sdPlaceCircle(f->start);
			return;
		}

		auto end = sdFindJoint(sdLayerPos(t));
		if (ccpDistance(f->start, end.pos) < 1.f) return;

		sdPlaceStick(f->start, end.pos);                 // stick first, circles on top
		if (!f->startExisting) sdPlaceCircle(f->start);
		if (!end.existing)     sdPlaceCircle(end.pos);
	}

	void ccTouchCancelled(CCTouch* t, CCEvent* e) {
		auto f = m_fields.self();
		if (!sd::g_drawMode || !f->active) return EditorUI::ccTouchCancelled(t, e);
		f->active = false;
		sdClearPreview();
	}
};

$on_mod(Loaded) {
	// NOTE: argument order was read from the Allium binary's symbols, check EditorTabs.hpp if it doesn't compile.
	alpha::editor_tabs::addTab(
		alpha::editor_tabs::BUILD,
		"stick-draw-tab"_spr,

		// 1) content of the tab (button bar)
		[]() -> CCNode* {
			std::vector<Ref<CCNode>> items;

			auto makeBtn = [&](char const* text, bool forStick) {
				auto spr = ButtonSprite::create(text);
				spr->setScale(0.6f);
				auto btn = CCMenuItemExt::createSpriteExtra(spr, [forStick](CCMenuItemSpriteExtra*) {
					sd::pickFromSelection(forStick);
				});
				items.push_back(btn);
			};
			makeBtn("Stick = selected", true);
			makeBtn("Circle = selected", false);

			return alpha::editor_tabs::createEditButtonBar(items);
		},

		// 2) tab icon
		[]() -> CCNode* {
			return CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
		},

		// 3) tab selected / deselected -> draw mode on / off
		[](bool selected, CCNode*) {
			sd::g_drawMode = selected;
		},

		// 4) page changed (unused)
		[](int, int, CCNode*) {}
	);
}
