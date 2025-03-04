#include "scripting.h"

#include <iostream>
#include <stdexcept>
#include <lua.hpp>

#include "../../files/engine_paths.h"
#include "../../files/files.h"
#include "../../util/timeutil.h"
#include "../../world/Level.h"
#include "../../voxels/Block.h"
#include "../../items/ItemDef.h"
#include "../../logic/BlocksController.h"
#include "../../engine.h"
#include "api_lua.h"

using namespace scripting;

namespace scripting {
    extern lua_State* L;
}

Engine* scripting::engine = nullptr;
lua_State* scripting::L = nullptr;
Level* scripting::level = nullptr;
const Content* scripting::content = nullptr;
BlocksController* scripting::blocks = nullptr;

inline int lua_pushivec3(lua_State* L, int x, int y, int z) {
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    lua_pushinteger(L, z);
    return 3;
}

void delete_global(lua_State* L, const char* name) {
    lua_pushnil(L);
    lua_setglobal(L, name);
}

bool rename_global(lua_State* L, const char* src, const char* dst) {
    lua_getglobal(L, src);
    if (lua_isnil(L, lua_gettop(L))) {
        lua_pop(L, lua_gettop(L));
        return false;
    }
    lua_setglobal(L, dst);
    delete_global(L, src);
    return true;
}

int call_func(lua_State* L, int argc, const std::string& name) {
    if (lua_pcall(L, argc, LUA_MULTRET, 0)) {
        std::cerr << "Lua error in " << name << ": ";
        std::cerr << lua_tostring(L,-1) << std::endl;
        return 0;
    }
    return 1;
}

void scripting::initialize(Engine* engine) {
    scripting::engine = engine;

    L = luaL_newstate();
    if (L == nullptr) {
        throw std::runtime_error("could not to initialize Lua");
    }
    
    // Allowed standard libraries
    luaopen_base(L);
    luaopen_math(L);
    luaopen_string(L);
    luaopen_table(L);

    // io-manipulations will be implemented via api functions
    
    std::cout << LUA_VERSION << std::endl;
#   ifdef LUAJIT_VERSION
        luaopen_jit(L);
        std::cout << LUAJIT_VERSION << std::endl;
#   endif // LUAJIT_VERSION

    apilua::create_funcs(L);
}

void scripting::on_world_load(Level* level, BlocksController* blocks) {
    scripting::level = level;
    scripting::content = level->content;
    scripting::blocks = blocks;
    auto paths = scripting::engine->getPaths();
    fs::path file = paths->getResources()/fs::path("scripts/world.lua");
    std::string src = files::read_string(file);
    luaL_loadbuffer(L, src.c_str(), src.length(), file.string().c_str());
    call_func(L, 0, "<script>");
}

void scripting::on_world_quit() {
    scripting::level = nullptr;
    scripting::content = nullptr;
}

void scripting::update_block(const Block* block, int x, int y, int z) {
    std::string name = block->name+".update";
    lua_getglobal(L, name.c_str());
    lua_pushivec3(L, x, y, z);
    call_func(L, 3, name);
}

void scripting::random_update_block(const Block* block, int x, int y, int z) {
    std::string name = block->name+".randupdate";
    lua_getglobal(L, name.c_str());
    lua_pushivec3(L, x, y, z);
    call_func(L, 3, name);
}

void scripting::on_block_placed(Player* player, const Block* block, int x, int y, int z) {
    std::string name = block->name+".placed";
    lua_getglobal(L, name.c_str());
    lua_pushivec3(L, x, y, z);
    lua_pushinteger(L, 1); // player id placeholder
    call_func(L, 4, name);
}

void scripting::on_block_broken(Player* player, const Block* block, int x, int y, int z) {
    std::string name = block->name+".broken";
    lua_getglobal(L, name.c_str());
    lua_pushivec3(L, x, y, z);
    lua_pushinteger(L, 1); // player id placeholder
    call_func(L, 4, name);
}

void scripting::on_block_interact(Player* player, const Block* block, int x, int y, int z) {
    std::string name = block->name+".oninteract";
    lua_getglobal(L, name.c_str());
    lua_pushivec3(L, x, y, z);
    lua_pushinteger(L, 1);
    call_func(L, 4, name);
}

bool scripting::on_item_use_on_block(Player* player, const ItemDef* item, int x, int y, int z) {
    std::string name = item->name+".useon";
    lua_getglobal(L, name.c_str());
    lua_pushivec3(L, x, y, z);
    lua_pushinteger(L, 1); // player id placeholder
    if (call_func(L, 4, name)) {
        return lua_toboolean(L, -1);
    }
    return false;
}

bool scripting::on_item_break_block(Player* player, const ItemDef* item, int x, int y, int z) {
    std::string name = item->name+".blockbreakby";
    lua_getglobal(L, name.c_str());
    lua_pushivec3(L, x, y, z);
    lua_pushinteger(L, 1); // player id placeholder
    if (call_func(L, 4, name)) {
        return lua_toboolean(L, -1);
    }
    return false;
}

// todo: refactor

void scripting::load_block_script(std::string prefix, fs::path file, block_funcs_set* funcsset) {
    std::string src = files::read_string(file);
    std::cout << "loading script " << file.u8string() << std::endl;
    if (luaL_loadbuffer(L, src.c_str(), src.size(), file.string().c_str())) {
        std::cerr << "Lua error:" << lua_tostring(L,-1) << std::endl;
        return;
    }
    call_func(L, 0, "<script>");
    funcsset->init=rename_global(L, "init", (prefix+".init").c_str());
    funcsset->update=rename_global(L, "on_update", (prefix+".update").c_str());
    funcsset->randupdate=rename_global(L, "on_random_update", (prefix+".randupdate").c_str());
    funcsset->onbroken=rename_global(L, "on_broken", (prefix+".broken").c_str());
    funcsset->onplaced=rename_global(L, "on_placed", (prefix+".placed").c_str());
    funcsset->oninteract=rename_global(L, "on_interact", (prefix+".oninteract").c_str());
}

void scripting::load_item_script(std::string prefix, fs::path file, item_funcs_set* funcsset) {
    std::string src = files::read_string(file);
    std::cout << "loading script " << file.u8string() << std::endl;
    if (luaL_loadbuffer(L, src.c_str(), src.size(), file.string().c_str())) {
        std::cerr << "Lua error:" << lua_tostring(L,-1) << std::endl;
        return;
    }
    call_func(L, 0, "<script>");
    funcsset->init=rename_global(L, "init", (prefix+".init").c_str());
    funcsset->on_use_on_block=rename_global(L, "on_use_on_block", (prefix+".useon").c_str());
    funcsset->on_block_break_by=rename_global(L, "on_block_break_by", (prefix+".blockbreakby").c_str());
}

void scripting::close() {
    lua_close(L);

    L = nullptr;
    content = nullptr;
    level = nullptr;
}
