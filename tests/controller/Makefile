.PHONY : all clean

targets := run_tests make_plots

all : $(targets)

summary :
	python convert.py controller.yaml summary.tex
	latexmk -pdf summary.tex
	latexmk -c

run_tests : controller.yaml
	python convert.py controller.yaml run_tests
	chmod +x run_tests

make_plots : controller.yaml
	python convert.py controller.yaml make_plots
	chmod +x make_plots

clean : 
	rm -f $(targets) summary.pdf
