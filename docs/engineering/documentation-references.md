# How other RTOS projects build documentation

**Inspection:** October 2026, official upstream repositories. These observations describe documentation tooling, not comparative RTOS correctness or current certification. Branch links can change; consult the cited configuration for current versions.

| Project | Observed documentation pipeline | Useful precedent |
|---|---|---|
| Zephyr | Sphinx configuration with Doxygen/Breathe integration and project extensions | Authored manuals plus extracted C API in one navigation/search system |
| Apache NuttX | Sphinx with Read the Docs theme, navigation and diagram extensions | Docs as version-controlled source with repeatable builds |
| RTEMS | Sphinx, separate manuals, shared configuration and book theme | User, engineering and qualification-oriented material can share one pipeline |
| RIOT | Doxygen with HTML/man/LaTeX targets, Graphviz and a pinned/checksummed Doxygen binary | Extracted API reference from source and explicit documentation-tool version control |

Primary sources inspected:

- [Zephyr doc/conf.py](https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/conf.py)
- [NuttX Documentation/conf.py](https://github.com/apache/nuttx/blob/master/Documentation/conf.py)
- [RTEMS common/conf.py](https://github.com/RTEMS/rtems-docs/blob/master/common/conf.py)
- [RIOT doc/doxygen/Makefile](https://github.com/RIOT-OS/RIOT/blob/master/doc/doxygen/Makefile)

The relevant split is between authored explanations/tutorials and generated reference information. Source extraction does not write a usable tutorial, decide supported scope or prove a requirement. The site should combine both while preserving those distinctions.

EmbLinkRTOS adopts Sphinx with Markdown support, Doxygen/Breathe and Sphinx Needs, matching the Sphinx direction previously recommended in Q16. Existing specifications remain readable Markdown. The requirement catalog is generated from those sources, with independent evidence status. We do not claim FreeRTOS or ThreadX uses this exact stack: the inspected ThreadX README did not establish its current publisher, and no authoritative FreeRTOS publishing configuration was verified in this setup.
