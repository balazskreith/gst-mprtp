git ls-files -co --exclude-standard ./ | grep '\.plot$' | xargs git add
git ls-files -co --exclude-standard ./ | grep '\.sh$' | xargs git add
git ls-files -co --exclude-standard ./ | grep '\.c$' | xargs git add
git ls-files -co --exclude-standard ./ | grep '\.h$' | xargs git add