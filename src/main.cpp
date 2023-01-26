#include "common.hpp"
#include "app.hpp"

IApp* make_game (Window& window) { return new App( window ); };
int main () {
	return run_game(make_game, "Logic Sim");
}
