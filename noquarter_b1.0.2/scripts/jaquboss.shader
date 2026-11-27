models/players/common/specGlow
{
	deformVertexes wave 1000 sin 1.75 1 0 .5
	cull none
	{
		map $whiteimage
		blendFunc gl_one gl_one
		rgbGen entity
	}
}

models/players/common/specGlowWeapon
{
	deformVertexes wave 1000 sin 1.75 1 0 .5
	cull none
	{
		map $whiteimage
		blendFunc gl_one gl_one
		rgbGen entity
	}
}

onScreenColor
{
	{
		map $whiteimage
		blendFunc gl_one gl_src_color
		rgbGen vertex
	}
}
models/mapobjects/blitz_sd/blitz_sd
{
	cull none
	{
		map textures/effects/envmap_slate_90.tga
		rgbGen lightingdiffuse
		tcGen environment
	}
	{
		map models/mapobjects/blitz_sd/blitz_sd.tga
		blendFunc GL_ONE GL_ONE_MINUS_SRC_ALPHA
		rgbGen lightingdiffuse
	}
}