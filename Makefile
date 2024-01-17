.SUFFIXES:
CCX     = g++
CXXFLAGS =-std=c++17

DEBUGARGS = -g3 -Wall -Wextra -Wconversion -Wdouble-promotion -Wno-unused-parameter -Wno-unused-function -Wno-sign-conversion 
LDFLAGS=-lSDL2main -lSDL2

EXE := filemap

ifeq ($(OS),Windows_NT)
	LDFLAGS := -lmingw32 $(LDFLAGS)
endif

SDL2_INCLUDES := $$(sdl2-config --cflags --libs)

IMGUI_DIR := ./external/imgui
IMGUI_SOURCE = $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp 	\
			   $(IMGUI_DIR)/backends/imgui_impl_sdlrenderer2.cpp 	\
			   $(IMGUI_DIR)/imgui_demo.cpp 	\
			   $(IMGUI_DIR)/imgui_draw.cpp 	\
			   $(IMGUI_DIR)/imgui_tables.cpp	\
			   $(IMGUI_DIR)/imgui_widgets.cpp	\
			   $(IMGUI_DIR)/imgui.cpp	\
			   $(IMGUI_DIR)/misc/cpp/imgui_stdlib.cpp

IMGUI_OBJS = $(IMGUI_SOURCE:.cpp=.o)


$(EXE): app.o $(IMGUI_OBJS)
	$(CCX) -o $@ app.o $(IMGUI_OBJS) $(LDFLAGS)
	
$(IMGUI_DIR)/%.o:$(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $< -I./external/imgui/

$(IMGUI_DIR)/backends/%.o:$(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $< -I./external/imgui/ $(SDL2_INCLUDES)


app.o: main.cpp debug.h filemap.h window.h filetree.h external/imgui/imgui.h
	$(CXX) -c main.cpp -o app.o $(CXXFLAGS) $(SDL2_INCLUDES) -Iexternal/imgui/ $(DEBUGARGS)
