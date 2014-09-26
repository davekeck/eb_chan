package main

import (
    "os"
    "fmt"
    "io/ioutil"
    "path"
    "path/filepath"
    "strings"
)

const kCmdName = "merge"
const kCmdFullName = "merge"
const kCmdVersion = "0.1"

/* A naive parser for quoted #includes. Can be tricked if the #include is inside a C++-style comment. */
func extractIncludeFilename(line string) string {
    tline := strings.TrimSpace(line)
    if !strings.HasPrefix(tline, "#include") {
        return ""
    }
    
    parts := strings.Split(tline, `"`)
    if len(parts) < 3 || strings.TrimSpace(parts[0]) != "#include" {
        return ""
    }
    
    return parts[1]
}

func filename2NormalizedPath(filename string) (string, error) {
    r, err := filepath.Abs(filename)
    if err != nil {
        return "", fmt.Errorf("filepath.Abs() failed: %v", err)
    }
    
    r = filepath.Clean(r)
    return r, nil
}

func substituteIncludes(path string, history map[string]bool) (string, error) {
    b, err := ioutil.ReadFile(path)
    if err != nil {
        return "", fmt.Errorf("ioutil.ReadFile() failed: %v", err)
    }
    
    lines := strings.Split(string(b), "\n")
    result := ""
    for _, line := range lines {
        incFilename := extractIncludeFilename(line)
        if incFilename != "" {
            /* We've found an #include "..." line, so generate the full file path to use as our key into our history map. */
            path, err := filename2NormalizedPath(incFilename)
            if err != nil {
                return "", err
            }
            
            /* If we haven't visited this header yet, paste its content in place of the #include. */
            if !history[path] {
                history[path] = true
                s, err := substituteIncludes(incFilename, history)
                if err != nil {
                    return "", err
                }
                result += s
            }
        } else {
            /* Not an #include "..." line */
            result += line+"\n"
        }
    }
    
    return result, nil
}

func main() {
	usage := fmt.Sprintf(`%v %v

Usage:
  %v file.c
`, kCmdFullName, kCmdVersion, kCmdName)
    
    if len(os.Args) <= 1 {
		fmt.Printf("%v", usage)
		os.Exit(1)
    }
    
    /* Get the path of the root file that was passed as an argument */
    rootFile := os.Args[1]
    
    /* Extract the file's parent directory path and the file's name */
    dir := path.Dir(rootFile)
    filename := path.Base(rootFile)
    
    /* Change to the directory where the file is */
    err := os.Chdir(dir)
    if err != nil {
		fmt.Printf("Failed to change working directory: %v\n", err)
		os.Exit(1)
    }
    
    r, err := substituteIncludes(filename, map[string]bool{})
    if err != nil {
		fmt.Printf("%v\n", err)
		os.Exit(1)
    }
    
    fmt.Printf("%v", r)
}
