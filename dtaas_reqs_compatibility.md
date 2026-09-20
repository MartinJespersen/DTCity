# DTaaS
## R1
- Should the adapters be something like a dynamically linked libraries?
## R4
- Where is the playhead located? Simulator, stream, visualization?
  - In DTCity Simulator controls the playhead.
  - What does the historian store and is it the controller of the play-a-head
  - How does ringbuffer when sim and real-time data arrives too fast for a more likely too infrequent
    - What to do in the remaining time between update? getValueAt would always be latest.
    - 
# R6 
- Per frame updates should not be a requirement
## R9 
- How does this work for native, where at least an integrated GPU is always precent.
- A requirement to use CPU only rendering will be a huge constrained in my opinion/experience.
- "Graceful degradation" is closely linked to rendering, but maybe the substrate should adapt to different hardware specs.

# Why is the Renderer Irrelevant to the architecture?
- Many of the constraints on what is possible will lie at this layer?
  - Control the interaction between CPU, GPU and Screen
- Constraints on the network/update speed?

# Architectural decisions:
- What state should there on the visualization side?
