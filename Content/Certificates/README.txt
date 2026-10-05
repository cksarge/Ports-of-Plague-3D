cacert.pem is the list of certificate authorities the packaged game trusts when it opens a secure
connection (multi-device play). It is the bundle that comes with Unreal Engine, copied unchanged from
Engine/Content/Certificates/ThirdParty: Mozilla's root certificates as extracted by the curl project
(https://curl.se/docs/caextract.html), under the Mozilla Public License 2.0.

The editor finds the engine's own copy; a packaged game only has what the project stages, so without this
file it cannot connect at all. To refresh it, copy the engine's file here again.
