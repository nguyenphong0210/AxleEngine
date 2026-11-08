# @PLUGIN_NAME@

Generated project plugin. It imports only the matched Axle plugin SDK hosts.
No Engine source list or Engine CMake change is needed.

1. Open the owning `.axleproj` in Editor and open **Plugins**.
2. Refresh, select **@PLUGIN_NAME@**, Apply, then **Build selected plugins**.
3. Restart to activate the verified generation. The current Editor stays open if
   compilation or candidate startup fails.
4. Create/select an entity, use **Add Component** to add `@PLUGIN_ID@/Mover`,
   edit **Speed** in its Inspector, save the scene and reopen it.
5. Play moves the runtime copy along X and draws a green debug line. Stop leaves
   the authored scene unchanged. Duplicate and Assembly use owned payload clones.

Runtime payloads own a `std::string`; their callbacks allocate/free in the same
DLL. Inspector uses a narrow Runtime export. Add reference remapping to `Clone`
before introducing entity references and increment the authored schema for an
incompatible payload. SDK, compiler and configuration identities must match the
Editor. Native modules activate at process startup; hot reload and static player
plugin composition are outside PLG-01..13.
