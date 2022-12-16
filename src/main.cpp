#include "common.hpp"

#include "game.hpp"
#include "opengl/renderer.hpp"

struct App : IApp {
	SERIALIZE(App, _window, game, renderer)

	virtual ~App () {}
	
	virtual void json_load () { load("debug.json", this); }
	virtual void json_save () { save("debug.json", *this); }

	Window& _window; // for serialization even though window has to exists out of App instance (different lifetimes)
	App (Window& w): _window{w} {}

	ogl::Renderer renderer;
	Game game;

	virtual void imgui (Window& window) {
		renderer.imgui(window.input);
		game.imgui(window.input);
	}
	virtual void frame (Window& window) {
		renderer.begin(window, game, window.input.window_size);
		game.update(window, renderer);
		renderer.end(window, game, window.input.window_size);
	}
};

IApp* make_game (Window& window) { return new App( window ); };
int main () {
	return run_game(make_game, "Logic Sim");
}
