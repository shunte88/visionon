package main

import "C"
import (
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"sync"

	log "github.com/sirupsen/logrus"
)

var count int
var nc *NotificationCenter
var mtx sync.Mutex

type (
	// Channel data - VU only first commit
	Channel struct {
		Name        string `json:"name"`
		Accumulated int32  `json:"accumulated,omitempty"`
		Scaled      int32  `json:"scaled,omitempty"`
	}
	// Meter visualization metrics
	Meter struct {
		Type     string    `json:"type,omitempty"`
		Channels []Channel `json:"channel,omitempty"`
	}
)

//export Initialize
func Initialize() {
	nc = NewNotificationCenter()
}

//export Publish
func Publish(meter, data string) int {

	if nc == nil {
		log.Error("Notifier is not Initialized")
		panic(`Not Initialized`)
	}
	mtx.Lock()
	count++

	// need to format this in C routine - or better share a struct
	var cc []Channel
	for _, v := range strings.Split(data, `|`) {
		vc := strings.Split(v, `:`)
		scaled, _ := strconv.ParseInt(vc[1], 10, 32)
		accum, _ := strconv.ParseInt(vc[2], 10, 32)
		cc = append(cc, Channel{Name: vc[0], Accumulated: int32(accum), Scaled: int32(scaled)})

	}

	m := Meter{Type: meter, Channels: cc}

	if payload, err := json.Marshal(m); err == nil {
		log.Debug(string(payload))
		if err := nc.Notify(payload); err != nil {
			panic(err)
		}
	}

	mtx.Unlock()
	return count
}

//export Serve
func Serve() {
	if nil == nc {
		Initialize()
	}
	port := 8022
	ep := `/visionon`
	log.Infof("Serve on localhost:%d%s\n", port, ep)
	http.HandleFunc(ep, handleSSE(nc))
	http.ListenAndServe(fmt.Sprintf(":%d", port), nil)
}

type UnsubscribeFunc func() error

type Subscriber interface {
	Subscribe(c chan []byte) (UnsubscribeFunc, error)
}

func handleSSE(s Subscriber) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		// Subscribe
		c := make(chan []byte)
		unsubscribeFn, err := s.Subscribe(c)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}

		// Signal SSE Support
		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("Connection", "keep-alive")
		w.Header().Set("Access-Control-Allow-Origin", "*")

	Looping:
		for {
			select {
			case <-r.Context().Done():
				if err := unsubscribeFn(); err != nil {
					http.Error(w, err.Error(), http.StatusInternalServerError)
					return
				}
				break Looping

			default:
				b := <-c
				fmt.Fprintf(w, "data: %v\n\n", string(b)) // the format here is specific
				w.(http.Flusher).Flush()
			}
		}
	}
}

type Notifier interface {
	Notify(b []byte) error
}

type NotificationCenter struct {
	subscribers   map[chan []byte]struct{}
	subscribersMu *sync.Mutex
}

func NewNotificationCenter() *NotificationCenter {
	return &NotificationCenter{
		subscribers:   map[chan []byte]struct{}{},
		subscribersMu: &sync.Mutex{},
	}
}

func (nc *NotificationCenter) Subscribe(c chan []byte) (UnsubscribeFunc, error) {
	nc.subscribersMu.Lock()
	nc.subscribers[c] = struct{}{}
	nc.subscribersMu.Unlock()

	unsubscribeFn := func() error {
		nc.subscribersMu.Lock()
		delete(nc.subscribers, c)
		nc.subscribersMu.Unlock()

		return nil
	}

	return unsubscribeFn, nil
}

func (nc *NotificationCenter) Notify(b []byte) error {
	nc.subscribersMu.Lock()
	for c := range nc.subscribers {
		select {
		case c <- b:
		default:
		}
	}
	// no defer overhead
	nc.subscribersMu.Unlock()
	return nil
}

func main() {}
