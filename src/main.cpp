#include "common.hpp"

#include "game.hpp"
#include "opengl/renderer.hpp"

struct App : IApp {
	friend void to_json(nlohmann::ordered_json& j, App const& t) {
		j["window"]   = t._window;
		j["game"]     = t.game;
		j["renderer"] = t.renderer;
	}
	friend void from_json(const nlohmann::ordered_json& j, App& t) {
		if (j.contains("window"))   j.at("window")  .get_to(t._window);
		if (j.contains("game"))     j.at("game")    .get_to(t.game);
		if (j.contains("renderer")) j.at("renderer").get_to(t.renderer);
	}

	virtual ~App () {}
	
	virtual void json_load () { load("debug.json", this); }
	virtual void json_save () { save("debug.json", *this); }

	Window& _window; // for serialization even though window has to exists out of App instance (different lifetimes)
	App (Window& w): _window{w} {}

	Game game;
	ogl::Renderer renderer;

	virtual void frame (Window& window) {
		ZoneScoped;
		
		game.imgui(window.input);
		renderer.imgui(window.input);

		game.update(window);
		renderer.render(window, game, window.input.window_size);
	}
};

IApp* make_game (Window& window) { return new App( window ); };
int main () {
	return run_game(make_game, "Logic Sim");
}
