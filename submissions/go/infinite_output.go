package main

import "fmt"

func main() {
	var a, b int64
	fmt.Scan(&a, &b)
	for {
		fmt.Println(a + b)
	}
}
