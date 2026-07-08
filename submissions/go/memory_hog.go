package main

func main() {
	var chunks [][]byte
	for {
		chunks = append(chunks, make([]byte,
			1024*1024))
	}
}
