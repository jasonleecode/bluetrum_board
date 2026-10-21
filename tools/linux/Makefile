.PHONY: check
PYTHON ?= python3
check:
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) -m unittest discover -s ../tests -p 'test_*.py' -v
