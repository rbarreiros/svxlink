# ChatGPT module events
namespace eval ChatGPT {

  # Play module activation sound
  proc module_activated {} {
    playMsg "ChatGPT" "module_activated"
  }

  # Play help message
  proc help {} {
    playMsg "ChatGPT" "help"
    playMsg "Core" "online_help"
  }

  # Play recording started
  proc recording_started {} {
    playMsg "ChatGPT" "recording_started" 
    playSilence 200
  }

  # Play recording stopped
  proc recording_stopped {} {
    playMsg "ChatGPT" "recording_stopped"
    playSilence 200
  }

  # Play processing request
  proc processing_request {} {
    playMsg "ChatGPT" "processing_request"
    playSilence 500
  }

  # Play ready for next
  proc ready_for_next {} {
    playMsg "ChatGPT" "ready_for_next"
    playSilence 200
  }

  # Play error message
  proc error_occurred {} {
    playMsg "ChatGPT" "error_occurred"
    playSilence 200
  }

  # Play timeout message  
  proc operation_timeout {} {
    playMsg "ChatGPT" "operation_timeout"
    playSilence 200
  }
}
