# Boarding

`phoenix.gameplay.boarding.state` owns boarding encounter, assessment, custody and objective continuation state. Its serializer is independently compiled in `serialization.cpp`; saved field names and strict reads retain their existing semantics.

World adapters still handle located entities, authority and cross-domain ownership effects. These adapters are migration work in progress; locating their headers here does not make them independent domain implementations.
