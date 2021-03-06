# Description of the variables
#
# ns : number of slow processes
# nf : number of fast processes
#
# g : solution of the cell problem (dimension = ns)
#
# h : Non-leading order part of the drift of the fast processes.
# (The one that multiplies 1/eps.)
#
# v : The potential of the problem
#
# s : Diffusion coefficient
#

## 2D problem

# user input : dimensions
ns = 2
nf = 2
# end

# user input : diffusion
s = sympy.sqrt(2)
# end

# user input : solution
g[0] = sympy.cos(x[0] + y[0] + y[1])
g[1] = sympy.sin(x[1]) * sympy.sin(y[0] + y[1]);
# end

# user input : second order drift
h[0] = sympy.cos(x[0]) * sympy.cos(y[0])  * sympy.cos(y[1]);
h[1] = sympy.cos(x[0]) * sympy.cos(y[0] + y[1]);
# end

# user input : potential
v = ((y[0] - 1*sympy.cos(0))**2 + (y[1] - 1*sympy.sin(0))**2) * ((y[0] - 1*sympy.cos(2 * sympy.pi / 3))**2 + (y[1] - 1*sympy.sin(2 * sympy.pi / 3))**2) * ((y[0] - 1*sympy.cos(4 * sympy.pi / 3))**2 + (y[1] - 1*sympy.sin(4 * sympy.pi / 3))**2)
# end

## Configuration of the spectral solver

# user input : scaling
sigma = 0.35
# end
