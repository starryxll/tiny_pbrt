cmake_minimum_required(VERSION 3.15)
project(tiny_pbrt)

set(CMAKE_CXX_STANDARD 14)

add_executable(tiny_pbrt main.cpp primitive.cpp primitive.h pbrt.h error.cpp error.h parallel.cpp parallel.h shape.cpp shape.h scene.cpp scene.h materials/bssrdf.cpp materials/bssrdf.h materials/subsurface.cpp materials/subsurface.h samplers/stratified.cpp samplers/stratified.h sampler.cpp sampler.h sampling.cpp sampling.h samplers/halton.cpp samplers/halton.h cameras/environment.cpp cameras/environment.h camera.cpp camera.h film.cpp film.h fileutil.cpp fileutil.h stats.cpp stats.h cameras/perspective.cpp cameras/perspective.h accelarator/kdtreeaccel.cpp accelarator/kdtreeaccel.h lights/diffuse.cpp lights/diffuse.h lights/distant.cpp lights/distant.h material.cpp material.h memory.cpp memory.h)