#include "core.hpp"
#include "common/dimension.hpp"
#include "logger/logger.hpp"
#include <memory>
#include "nlohmann/json.hpp"
#include "imp/action_catalog.hpp"
#include "util/util.hpp"
#include "util/str_util.hpp"
#include <glibmm/ustring.h>
#include <glibmm/fileutils.h>

namespace horizon {

ToolResponse Core::tool_begin(ToolID tool_id, const ToolArgs &args, class ImpInterface *imp, bool transient)
{
    if (tool_is_active()) {
        throw std::runtime_error("can't begin tool while tool is active");
        return ToolResponse::end();
    }

    update_rules(); // write rules to board, so tool has the current rules
    try {
        tool = create_tool(tool_id);
        {
            auto sp = tool->get_settings_proxy();
            if (sp != nullptr) {
                auto tid = tool->get_tool_id_for_settings();
                auto j = s_signal_load_tool_settings.emit(tid);
                if (j != nullptr)
                    sp->load_from_json(j);
            }
        }
        tool->set_imp_interface(imp);
        if (!args.keep_selection) {
            tool->selection.clear();
            tool->selection = args.selection;
        }
        if (transient)
            tool->set_transient();
        if (!tool->can_begin()) { // check if we can actually use this tool
            tool.reset();
            return ToolResponse();
        }
    }
    catch (const std::exception &e) {
        Logger::log_critical("exception thrown in tool constructor of "
                                     + action_catalog.at({ActionID::TOOL, tool_id}).name,
                             Logger::Domain::CORE, e.what());
        return ToolResponse::end();
    }
    if (tool) {
        s_signal_tool_changed.emit(tool_id);
        ToolResponse r;
        try {
            r = tool->begin(args);
        }
        catch (const std::exception &e) {
            tool.reset();
            s_signal_tool_changed.emit(ToolID::NONE);
            Logger::log_critical("exception thrown in tool_begin of "
                                         + action_catalog.at({ActionID::TOOL, tool_id}).name,
                                 Logger::Domain::CORE, e.what());
            return ToolResponse::end();
        }
        maybe_end_tool(r);

        return r;
    }

    return ToolResponse();
}

void Core::maybe_end_tool(const ToolResponse &r)
{
    if (r.result != ToolResponse::Result::NOP) { // end tool
        auto tid = tool->get_tool_id_for_settings();
        auto settings = tool->get_settings_const();
        if (settings)
            s_signal_save_tool_settings.emit(tid, settings->serialize());
        tool_selection = tool->selection;
        tool.reset();
        s_signal_tool_changed.emit(ToolID::NONE);
        if (r.result == ToolResponse::Result::COMMIT) {
            set_needs_save(true);
            rebuild();
        }
        else if (r.result == ToolResponse::Result::REVERT) {
            history_load(history_current);
            rebuild(true);
        }
        else if (r.result == ToolResponse::Result::END) { // did nothing
            // do nothing
        }
    }
}

std::set<SelectableRef> &Core::get_tool_selection()
{
    if (tool)
        return tool->selection;
    else
        return tool_selection;
}

std::pair<bool, bool> Core::tool_can_begin(ToolID tool_id, const std::set<SelectableRef> &sel)
{
    auto t = create_tool(tool_id);
    t->selection = sel;
    auto r = t->can_begin();
    auto s = t->is_specific();
    return {r, s};
}

bool Core::tool_handles_esc()
{
    if (tool) {
        return tool->handles_esc();
    }
    return false;
}

ToolResponse Core::tool_update(const ToolArgs &args)
{
    if (tool) {
        ToolResponse r;
        try {
            r = tool->update(args);
        }
        catch (const std::exception &e) {
            tool.reset();
            s_signal_tool_changed.emit(ToolID::NONE);
            Logger::log_critical("exception thrown in tool_update", Logger::Domain::CORE, e.what());
            return ToolResponse::end();
        }
        maybe_end_tool(r);
        return r;
    }
    return ToolResponse();
}

Junction *Core::insert_junction(const UUID &uu)
{
    auto map = get_junction_map();
    auto x = map->emplace(std::make_pair(uu, uu));
    return &(x.first->second);
}

Junction *Core::get_junction(const UUID &uu)
{
    auto map = get_junction_map();
    return &map->at(uu);
}

void Core::delete_junction(const UUID &uu)
{
    auto map = get_junction_map();
    map->erase(uu);
}

Line *Core::insert_line(const UUID &uu)
{
    auto map = get_line_map();
    auto x = map->emplace(std::make_pair(uu, uu));
    return &(x.first->second);
}

Line *Core::get_line(const UUID &uu)
{
    auto map = get_line_map();
    return &map->at(uu);
}

void Core::delete_line(const UUID &uu)
{
    auto map = get_line_map();
    map->erase(uu);
}

std::vector<Line *> Core::get_lines()
{
    auto *map = get_line_map();
    std::vector<Line *> r;
    if (!map)
        return r;
    for (auto &it : *map) {
        r.push_back(&it.second);
    }
    return r;
}

Arc *Core::insert_arc(const UUID &uu)
{
    auto map = get_arc_map();
    auto x = map->emplace(std::make_pair(uu, uu));
    return &(x.first->second);
}

Arc *Core::get_arc(const UUID &uu)
{
    auto map = get_arc_map();
    return &map->at(uu);
}

void Core::delete_arc(const UUID &uu)
{
    auto map = get_arc_map();
    map->erase(uu);
}

std::vector<Arc *> Core::get_arcs()
{
    auto *map = get_arc_map();
    std::vector<Arc *> r;
    if (!map)
        return r;
    for (auto &it : *map) {
        r.push_back(&it.second);
    }
    return r;
}

Text *Core::insert_text(const UUID &uu)
{
    auto map = get_text_map();
    auto x = map->emplace(uu, uu);
    return &(x.first->second);
}

Text *Core::get_text(const UUID &uu)
{
    auto map = get_text_map();
    return &map->at(uu);
}

void Core::delete_text(const UUID &uu)
{
    auto map = get_text_map();
    map->erase(uu);
}

Polygon *Core::insert_polygon(const UUID &uu)
{
    auto map = get_polygon_map();
    auto x = map->emplace(std::make_pair(uu, uu));
    return &(x.first->second);
}

Polygon *Core::get_polygon(const UUID &uu)
{
    auto map = get_polygon_map();
    return &map->at(uu);
}

void Core::delete_polygon(const UUID &uu)
{
    auto map = get_polygon_map();
    map->erase(uu);
}

Hole *Core::insert_hole(const UUID &uu)
{
    auto map = get_hole_map();
    auto x = map->emplace(std::make_pair(uu, uu));
    return &(x.first->second);
}

Hole *Core::get_hole(const UUID &uu)
{
    auto map = get_hole_map();
    return &map->at(uu);
}

void Core::delete_hole(const UUID &uu)
{
    auto map = get_hole_map();
    map->erase(uu);
}

Dimension *Core::insert_dimension(const UUID &uu)
{
    auto map = get_dimension_map();
    auto x = map->emplace(std::make_pair(uu, uu));
    return &(x.first->second);
}

Dimension *Core::get_dimension(const UUID &uu)
{
    auto map = get_dimension_map();
    return &map->at(uu);
}

void Core::delete_dimension(const UUID &uu)
{
    auto map = get_dimension_map();
    map->erase(uu);
}

Keepout *Core::insert_keepout(const UUID &uu)
{
    auto map = get_keepout_map();
    auto x = map->emplace(std::make_pair(uu, uu));
    return &(x.first->second);
}

Keepout *Core::get_keepout(const UUID &uu)
{
    auto map = get_keepout_map();
    return &map->at(uu);
}

void Core::delete_keepout(const UUID &uu)
{
    auto map = get_keepout_map();
    map->erase(uu);
}

std::vector<Keepout *> Core::get_keepouts()
{
    auto *map = get_keepout_map();
    std::vector<Keepout *> r;
    if (!map)
        return r;
    for (auto &it : *map) {
        r.push_back(&it.second);
    }
    return r;
}


void Core::rebuild(bool from_undo)
{
    if (!from_undo) {
        while (history_current + 1 != (int)history.size()) {
            history.pop_back();
        }
        assert(history_current + 1 == (int)history.size());
        history_push();
        history_current++;
        history_trim();
    }
    s_signal_rebuilt.emit();
    signal_can_undo_redo().emit();
}

void Core::undo()
{
    if (history_current) {
        history_current--;
        history_load(history_current);
        signal_rebuilt().emit();
        signal_can_undo_redo().emit();
        set_needs_save();
    }
}

void Core::redo()
{
    if (history_current + 1 == (int)history.size())
        return;
    std::cout << "can redo" << std::endl;
    history_current++;
    history_load(history_current);
    signal_rebuilt().emit();
    signal_can_undo_redo().emit();
    set_needs_save();
}

void Core::history_clear()
{
    history.clear();
    history_current = -1;
    signal_can_undo_redo().emit();
}

void Core::history_trim()
{
    const size_t history_max = 50;
    while (history.size() > history_max) {
        history.pop_front();
        history_current--;
    }
}

bool Core::can_redo() const
{
    return history_current + 1 != (int)history.size();
}

bool Core::can_undo() const
{
    return history_current;
}

void Core::set_property_begin()
{
    if (property_transaction)
        throw std::runtime_error("transaction already in progress");
    property_transaction = true;
}

void Core::set_property_commit()
{
    if (!property_transaction)
        throw std::runtime_error("no transaction in progress");
    rebuild();
    set_needs_save(true);
    property_transaction = false;
}

bool Core::get_property_transaction() const
{
    return property_transaction;
}

json Core::get_meta()
{
    json j;
    return j;
}

json Core::get_meta_from_file(const std::string &filename)
{
    auto j = load_json_from_file(filename);
    if (j.count("_imp")) {
        return j["_imp"];
    }
    return nullptr;
}

void Core::set_needs_save(bool v)
{
    if (v)
        s_signal_modified.emit();

    if (v != needs_save) {
        needs_save = v;
        s_signal_needs_save.emit(v);
    }
}

void Core::set_needs_save()
{
    set_needs_save(true);
}

bool Core::get_needs_save() const
{
    return needs_save;
}

void Core::save()
{
    save("");
    delete_autosave();
    set_needs_save(false);
}

const std::string Core::autosave_suffix = ".autosave";

void Core::autosave()
{
    save(autosave_suffix);
}

void Core::sort_search_results(std::list<Core::SearchResult> &results, const SearchQuery &q)
{
    results.sort([this, q](const auto &a, const auto &b) {
        auto da = this->get_display_name(a.type, a.uuid, a.sheet);
        auto db = this->get_display_name(b.type, b.uuid, b.sheet);
        auto ina = !Coordf(a.location).in_range(q.area_visible.first, q.area_visible.second);
        auto inb = !Coordf(b.location).in_range(q.area_visible.first, q.area_visible.second);

        if (ina > inb)
            return false;
        else if (ina < inb)
            return true;

        if (a.type > b.type)
            return false;
        else if (a.type < b.type)
            return true;

        auto c = strcmp_natural(da, db);
        if (c > 0)
            return false;
        else if (c < 0)
            return true;

        if (a.location.x > b.location.x)
            return false;
        else if (a.location.x < b.location.x)
            return true;

        return a.location.y > b.location.y;
    });
}

void Core::SearchQuery::set_query(const std::string &q)
{
    query = Glib::ustring(q).casefold();
    trim(query);
}

const std::string &Core::SearchQuery::get_query() const
{
    return query;
}

bool Core::SearchQuery::contains(const std::string &haystack) const
{
    Glib::ustring uhaystack(haystack);
    return uhaystack.casefold().find(query) != Glib::ustring::npos;
}

} // namespace horizon
