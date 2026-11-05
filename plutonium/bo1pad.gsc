// bo1pad - turns off the console stick acceleration and response graph in Plutonium,
// same as typing the dvars in the console. Skips online games.

init()
{
	if ( getDvarInt( "onlinegame" ) )
	{
		return;
	}

	setDvar( "aim_accel_turnrate_enabled", 0 );
	setDvar( "aim_input_graph_enabled", 0 );
}
