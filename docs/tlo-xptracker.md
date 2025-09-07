---
tags:
  - tlo
---
# `XPTracker`

<!--tlo-desc-start-->
Has an index that can return just the XP or AA for members.
<!--tlo-desc-end-->

## Forms
<!--tlo-forms-start-->
### {{ renderMember(type='xptracker', name='XPTracker') }}

:   Returns True if plugin is loaded

### {{ renderMember(type='xptracker', name='XPTracker', params='aa') }}

:   Limits results to only AA exp. e.g. `${XPTracker[aa].AveragePct}`

### {{ renderMember(type='xptracker', name='XPTracker', params='xp') }}

:   Limits results to normal exp. e.g. `${XPTracker[xp].PctExpPerHour}`

<!--tlo-forms-end-->

## Associated DataTypes
<!--tlo-datatypes-start-->
## [`xptracker`](datatype-xptracker.md)
{% include-markdown "projects/mq2xptracker/datatype-xptracker.md" start="<!--dt-desc-start-->" end="<!--dt-desc-end-->" trailing-newlines=false %} {{ readMore('projects/mq2xptracker/datatype-xptracker.md') }}
:    <h3>Members</h3>
    {% include-markdown "projects/mq2xptracker/datatype-xptracker.md" start="<!--dt-members-start-->" end="<!--dt-members-end-->" %}
    {% include-markdown "projects/mq2xptracker/datatype-xptracker.md" start="<!--dt-linkrefs-start-->" end="<!--dt-linkrefs-end-->" %}
    <!--tlo-datatypes-end-->

    <!--tlo-linkrefs-start-->
    [xptracker]: datatype-xptracker.md
    <!--tlo-linkrefs-end-->