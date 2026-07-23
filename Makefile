LUA_DIR=third_party\lua
SRC_DIR=src
BUILD_DIR=build
LUA_BUILD_DIR=$(BUILD_DIR)\lua
RELEASE_DIR=release
GOLDEN_AUDIO_DIR=$(RELEASE_DIR)\golden_audio

LUA_OBJECTS=$(LUA_BUILD_DIR)\lapi.obj $(LUA_BUILD_DIR)\lauxlib.obj $(LUA_BUILD_DIR)\lbaselib.obj $(LUA_BUILD_DIR)\lcode.obj $(LUA_BUILD_DIR)\lcorolib.obj $(LUA_BUILD_DIR)\lctype.obj $(LUA_BUILD_DIR)\ldblib.obj $(LUA_BUILD_DIR)\ldebug.obj $(LUA_BUILD_DIR)\ldo.obj $(LUA_BUILD_DIR)\ldump.obj $(LUA_BUILD_DIR)\lfunc.obj $(LUA_BUILD_DIR)\lgc.obj $(LUA_BUILD_DIR)\linit.obj $(LUA_BUILD_DIR)\liolib.obj $(LUA_BUILD_DIR)\llex.obj $(LUA_BUILD_DIR)\lmathlib.obj $(LUA_BUILD_DIR)\lmem.obj $(LUA_BUILD_DIR)\loadlib.obj $(LUA_BUILD_DIR)\lobject.obj $(LUA_BUILD_DIR)\lopcodes.obj $(LUA_BUILD_DIR)\loslib.obj $(LUA_BUILD_DIR)\lparser.obj $(LUA_BUILD_DIR)\lstate.obj $(LUA_BUILD_DIR)\lstring.obj $(LUA_BUILD_DIR)\lstrlib.obj $(LUA_BUILD_DIR)\ltable.obj $(LUA_BUILD_DIR)\ltablib.obj $(LUA_BUILD_DIR)\ltm.obj $(LUA_BUILD_DIR)\lundump.obj $(LUA_BUILD_DIR)\lutf8lib.obj $(LUA_BUILD_DIR)\lvm.obj $(LUA_BUILD_DIR)\lzio.obj
GAME_OBJECTS=$(BUILD_DIR)\app.obj $(BUILD_DIR)\audio.obj $(BUILD_DIR)\state.obj $(BUILD_DIR)\world.obj $(BUILD_DIR)\gameplay.obj $(BUILD_DIR)\rendering.obj $(BUILD_DIR)\text_renderer.obj
GOLDEN_AUDIO_FILES=$(GOLDEN_AUDIO_DIR)\laserShoot.wav $(GOLDEN_AUDIO_DIR)\hitEnemy.wav $(GOLDEN_AUDIO_DIR)\hitHurt.wav $(GOLDEN_AUDIO_DIR)\explosion.wav

all: dirs $(GOLDEN_AUDIO_FILES) $(RELEASE_DIR)\the-meta-game.exe

dirs:
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	@if not exist $(LUA_BUILD_DIR) mkdir $(LUA_BUILD_DIR)
	@if not exist $(RELEASE_DIR) mkdir $(RELEASE_DIR)
	@if not exist $(GOLDEN_AUDIO_DIR) mkdir $(GOLDEN_AUDIO_DIR)

{$(LUA_DIR)}.c{$(LUA_BUILD_DIR)}.obj:
	cl /nologo /c /O2 /W3 /TC /D_CRT_SECURE_NO_WARNINGS /I$(LUA_DIR) /Fo$@ $<

{$(SRC_DIR)}.cpp{$(BUILD_DIR)}.obj:
	cl /nologo /c /std:c++17 /EHsc /O2 /W4 /DNOMINMAX /DUNICODE /D_UNICODE /I. /I$(SRC_DIR) /I$(LUA_DIR) /Fo$@ $<

{.}.cpp{$(BUILD_DIR)}.obj:
	cl /nologo /c /std:c++17 /EHsc /O2 /W4 /DNOMINMAX /DUNICODE /D_UNICODE /I. /I$(SRC_DIR) /I$(LUA_DIR) /Fo$@ $<

$(BUILD_DIR)\app.obj: $(SRC_DIR)\app.cpp $(SRC_DIR)\audio.h $(SRC_DIR)\state.h $(SRC_DIR)\world.h $(SRC_DIR)\gameplay.h $(SRC_DIR)\rendering.h
$(BUILD_DIR)\audio.obj: $(SRC_DIR)\audio.cpp $(SRC_DIR)\audio.h
$(BUILD_DIR)\state.obj: $(SRC_DIR)\state.cpp $(SRC_DIR)\state.h
$(BUILD_DIR)\world.obj: $(SRC_DIR)\world.cpp $(SRC_DIR)\world.h $(SRC_DIR)\state.h text_renderer.h
$(BUILD_DIR)\gameplay.obj: $(SRC_DIR)\gameplay.cpp $(SRC_DIR)\audio.h $(SRC_DIR)\gameplay.h $(SRC_DIR)\world.h $(SRC_DIR)\state.h text_renderer.h
$(BUILD_DIR)\rendering.obj: $(SRC_DIR)\rendering.cpp $(SRC_DIR)\rendering.h $(SRC_DIR)\gameplay.h $(SRC_DIR)\world.h $(SRC_DIR)\state.h text_renderer.h
$(BUILD_DIR)\text_renderer.obj: text_renderer.cpp text_renderer.h

$(BUILD_DIR)\lua.lib: $(LUA_OBJECTS)
	lib /nologo /out:$@ $(LUA_OBJECTS)

$(GOLDEN_AUDIO_DIR)\laserShoot.wav: laserShoot.wav
	@copy /y laserShoot.wav $@ >nul
$(GOLDEN_AUDIO_DIR)\hitEnemy.wav: hitEnemy.wav
	@copy /y hitEnemy.wav $@ >nul
$(GOLDEN_AUDIO_DIR)\hitHurt.wav: hitHurt.wav
	@copy /y hitHurt.wav $@ >nul
$(GOLDEN_AUDIO_DIR)\explosion.wav: explosion.wav
	@copy /y explosion.wav $@ >nul

$(RELEASE_DIR)\the-meta-game.exe: $(GAME_OBJECTS) $(BUILD_DIR)\lua.lib
	link /nologo /SUBSYSTEM:WINDOWS /out:$@ $(GAME_OBJECTS) $(BUILD_DIR)\lua.lib user32.lib gdi32.lib dsound.lib dxguid.lib
	@echo Built $@

clean:
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
	@if exist $(RELEASE_DIR)\the-meta-game.exe del /q $(RELEASE_DIR)\the-meta-game.exe
	@if exist $(GOLDEN_AUDIO_DIR) rmdir /s /q $(GOLDEN_AUDIO_DIR)
