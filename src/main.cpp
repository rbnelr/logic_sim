#include "common.hpp"

#include "game.hpp"

extern std::unique_ptr<RendererBackend> make_ogl_renderer ();

struct App : IApp {
	friend void to_json(nlohmann::ordered_json& j, App const& t) {
		j["window"]   = t._window;
		j["game"]     = t.game;
		t.renderer->to_json(j["renderer"]);
	}
	friend void from_json(const nlohmann::ordered_json& j, App& t) {
		if (j.contains("window"))   j.at("window")  .get_to(t._window);
		if (j.contains("game"))     j.at("game")    .get_to(t.game);
		if (j.contains("renderer")) t.renderer->from_json(j.at("renderer"));
	}

	virtual ~App () {}
	
	virtual void json_load () { load("debug.json", this); }
	virtual void json_save () { save("debug.json", *this); }

	Window& _window; // for serialization even though window has to exists out of App instance (different lifetimes)
	App (Window& w): _window{w} {}

	Game game;
	std::unique_ptr<RendererBackend> renderer = make_ogl_renderer();

	virtual void frame (Window& window) {
		ZoneScoped;
		
		game.imgui(window.input);
		renderer->imgui(window.input);

		game.update(window);
		renderer->render(window, game, window.input.window_size);
	}
};

IApp* make_game (Window& window) { return new App( window ); };
int main () {
	return run_game(make_game, "Logic Sim");
}
