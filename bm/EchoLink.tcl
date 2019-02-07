# /opt/SVXLink/share/svxlink/events.d/local/EchoLink.tcl

namespace eval EchoLink {
  proc playMsg {msg} { }
  proc playNumber {number} { }
  proc playSilence {ms} { }

  proc activating_module {} { }
  proc deactivating_module {} { }
  proc timeout {} { }

  proc connecting_to {call} { playSilence 500; }
  proc connected {call} { }

  proc disconnected {call} { }
  proc remote_connected {call} { }
  proc remote_greeting {call} { }

  proc link_inactivity_timeout {} { }
  proc station_id_not_found {station_id} { }
}
