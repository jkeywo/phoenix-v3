# Protocol contracts precede host/session composition. All targets retain their
# historical names so executable and test linkage does not change.
add_subdirectory(consensus)
add_subdirectory(wire)
add_subdirectory(inference)
add_subdirectory(session-api)
add_subdirectory(durability)
add_subdirectory(local-session)
add_subdirectory(fleet)
add_subdirectory(remote-session)
add_subdirectory(projection)
add_subdirectory(lobby)
