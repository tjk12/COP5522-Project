## Top-level Makefile: builds all subprojects by delegating to their own Makefiles
# Targets:
#   all   - build all subprojects
#   bseq  - build 1_bseq_proj
#   oseq  - build 2_oseq_proj
#   bpar  - build 3_bpar_proj
#   opar  - build 4_opar_proj (falls back to 4_oseq_proj)
#   clean - run clean in each subproject
#   help  - show this help text

.PHONY: all bseq oseq bpar opar clean help

all: bseq oseq bpar opar

bseq:
	@echo "Building 1_bseq_proj"
	@$(MAKE) -C 1_bseq_proj

oseq:
	@echo "Building 2_oseq_proj"
	@$(MAKE) -C 2_oseq_proj

bpar:
	@echo "Building 3_bpar_proj"
	@$(MAKE) -C 3_bpar_proj

opar:
	@echo "Building 4_opar_proj (falls back to 4_oseq_proj if needed)"
	@if [ -d 4_opar_proj ]; then \
		$(MAKE) -C 4_opar_proj; \
	elif [ -d 4_oseq_proj ]; then \
		$(MAKE) -C 4_oseq_proj; \
	else \
		echo "Error: neither 4_opar_proj nor 4_oseq_proj found"; exit 1; \
	fi

clean:
	@echo "Cleaning subprojects (if present)"
	@for d in 1_bseq_proj 2_oseq_proj 3_bpar_proj 4_opar_proj 4_oseq_proj; do \
		if [ -d $$d ]; then \
			$(MAKE) -C $$d clean || true; \
		fi; \
	done

help:
	@echo "Usage: make [target]"
	@echo "Targets:"
	@echo "  all   - build all subprojects"
	@echo "  bseq  - build 1_bseq_proj"
	@echo "  oseq  - build 2_oseq_proj"
	@echo "  bpar  - build 3_bpar_proj"
	@echo "  opar  - build 4_opar_proj or 4_oseq_proj"
	@echo "  clean - run clean in each subproject"
