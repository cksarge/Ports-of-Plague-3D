# How the disk image is laid out (read by dmgbuild; see Tools/make_dmg.sh).
import os

app = defines['app']
application = os.path.basename(app)
format = 'UDZO'
files = [app]
symlinks = {'Applications': '/Applications'}
icon = defines['icon']
background = defines['background']
window_rect = ((200, 160), (660, 420))
default_view = 'icon-view'
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
show_icon_preview = False
include_icon_view_settings = True
arrange_by = None
icon_size = 120
text_size = 13
# The same places the arrow in the background runs between (Tools/dmg/make_background.py).
icon_locations = {application: (170, 235), 'Applications': (490, 235)}
