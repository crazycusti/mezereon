void init(void)
{
	// debug
	const char hw[] = "videolevel";
	int i;
	char* video = (char*) 0xb8000;
	
	for (i = 0; hw[i];i = 0) {
			video [i * 2] = hw[i];
			// <Commodore-Freak> schwarz gelb is beste farbenkombination - <Wynton> stimmt, bvb
			video [i * 2 + 1] = 0xE;
	}
}
